#include "ota.h"

bool Ota::performUpdate(const String &url) {
    Serial.printf("OTA from %s\n", url.c_str());
    WiFiClient client;
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("OTA failed: %s\n", httpUpdate.getLastErrorString().c_str());
            return false;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("OTA: no updates");
            return false;
        case HTTP_UPDATE_OK:
            Serial.println("OTA OK, rebooting");
            return true;
    }
    return false;
}
