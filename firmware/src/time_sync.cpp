#include "time_sync.h"

#include <Preferences.h>
#include <WiFi.h>
#include <cctype>
#include <cstring>
#include <time.h>

static const char *PREFS_NS = "phub";
static const char *KEY_TZ_OFFSET = "tz_ofs";
static const char *KEY_TZ_KNOWN = "tz_ok";

static bool synced_ = false;
static bool tzKnown_ = false;
static double epochBase_ = 0;
static unsigned long millisBase_ = 0;
static unsigned long lastTryMs_ = 0;
static int32_t utcOffsetSec_ = 0;

void TimeSync::begin() {
    Preferences prefs;
    if (prefs.begin(PREFS_NS, true)) {
        utcOffsetSec_ = prefs.getInt(KEY_TZ_OFFSET, 0);
        if (prefs.isKey(KEY_TZ_KNOWN)) {
            tzKnown_ = prefs.getBool(KEY_TZ_KNOWN, false);
        } else if (prefs.isKey(KEY_TZ_OFFSET)) {
            tzKnown_ = true;
        }
        prefs.end();
    }
    if (tzKnown_) {
        Serial.printf("TZ offset loaded: %+d sec\n", utcOffsetSec_);
    }
}

int32_t TimeSync::utcOffsetSec() { return utcOffsetSec_; }

void TimeSync::setUtcOffsetSec(int32_t sec) {
    if (sec < -43200 || sec > 50400) return;
    const bool offsetChanged = sec != utcOffsetSec_;
    const bool wasUnknown = !tzKnown_;
    if (!offsetChanged && !wasUnknown) return;

    utcOffsetSec_ = sec;
    tzKnown_ = true;
    Preferences prefs;
    if (prefs.begin(PREFS_NS, false)) {
        prefs.putInt(KEY_TZ_OFFSET, sec);
        prefs.putBool(KEY_TZ_KNOWN, true);
        prefs.end();
    }
    Serial.printf("TZ offset: %+d sec\n", sec);
}

static bool parseClockHm(const char *text, int &hour, int &min) {
    if (!text || std::strlen(text) != 5 || text[2] != ':') return false;
    if (!std::isdigit(static_cast<unsigned char>(text[0])) ||
        !std::isdigit(static_cast<unsigned char>(text[1])) ||
        !std::isdigit(static_cast<unsigned char>(text[3])) ||
        !std::isdigit(static_cast<unsigned char>(text[4]))) {
        return false;
    }
    hour = (text[0] - '0') * 10 + (text[1] - '0');
    min = (text[3] - '0') * 10 + (text[4] - '0');
    return hour >= 0 && hour <= 23 && min >= 0 && min <= 59;
}

void TimeSync::learnFromMacClock(const String &clockText) {
    if (!synced_) return;
    int macH = 0;
    int macM = 0;
    if (!parseClockHm(clockText.c_str(), macH, macM)) return;

    const time_t utc = static_cast<time_t>(nowUnix());
    struct tm utcTm {};
    if (!gmtime_r(&utc, &utcTm)) return;

    const int macSec = macH * 3600 + macM * 60;
    int utcSec = utcTm.tm_hour * 3600 + utcTm.tm_min * 60;
    int diff = macSec - utcSec;
    if (diff > 14 * 3600) diff -= 24 * 3600;
    if (diff < -12 * 3600) diff += 24 * 3600;
    if (diff < -43200 || diff > 50400) return;

    setUtcOffsetSec(diff);
}

bool TimeSync::trySync() {
    if (synced_) return true;
    if (WiFi.status() != WL_CONNECTED) return false;

    const unsigned long now = millis();
    if (lastTryMs_ != 0 && now - lastTryMs_ < 30000) return false;
    lastTryMs_ = now;

    configTime(0, 0, "pool.ntp.org", "time.google.com");
    struct tm timeinfo {};
    if (!getLocalTime(&timeinfo, 200)) return false;

    epochBase_ = static_cast<double>(mktime(&timeinfo));
    millisBase_ = now;
    synced_ = true;
    Serial.printf("NTP OK: %.0f\n", epochBase_);
    return true;
}

bool TimeSync::ensure() {
    if (synced_) return true;
    for (int i = 0; i < 3; ++i) {
        lastTryMs_ = 0;
        if (trySync()) return true;
        delay(100);
    }
    Serial.println("NTP failed");
    return false;
}

bool TimeSync::ready() { return synced_; }

double TimeSync::nowUnix() {
    if (!synced_) return 0;
    return epochBase_ + (millis() - millisBase_) / 1000.0;
}

String TimeSync::formatClockLocal() {
    if (!synced_) return String("--:--");
    const time_t t = static_cast<time_t>(nowUnix() + utcOffsetSec_);
    struct tm timeinfo {};
    if (!gmtime_r(&t, &timeinfo)) return String("--:--");
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
}
