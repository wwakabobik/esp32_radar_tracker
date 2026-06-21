#include "time_sync.h"

#include <WiFi.h>
#include <time.h>

static bool synced_ = false;
static double epochBase_ = 0;
static unsigned long millisBase_ = 0;
static unsigned long lastTryMs_ = 0;

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
    const time_t t = static_cast<time_t>(nowUnix());
    struct tm timeinfo {};
    if (!localtime_r(&t, &timeinfo)) return String("--:--");
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
}
