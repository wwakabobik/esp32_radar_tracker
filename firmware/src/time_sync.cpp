#include "time_sync.h"

#include <WiFi.h>
#include <time.h>

static bool synced_ = false;
static double epochBase_ = 0;
static unsigned long millisBase_ = 0;

bool TimeSync::ensure() {
    if (synced_) return true;
    if (WiFi.status() != WL_CONNECTED) return false;

    configTime(0, 0, "pool.ntp.org", "time.google.com");
    struct tm timeinfo {};
    for (int i = 0; i < 20; ++i) {
        if (getLocalTime(&timeinfo, 500)) {
            epochBase_ = static_cast<double>(mktime(&timeinfo));
            millisBase_ = millis();
            synced_ = true;
            Serial.printf("NTP OK: %.0f\n", epochBase_);
            return true;
        }
        delay(250);
    }
    Serial.println("NTP failed");
    return false;
}

bool TimeSync::ready() { return synced_; }

double TimeSync::nowUnix() {
    if (!synced_) return 0;
    return epochBase_ + (millis() - millisBase_) / 1000.0;
}
