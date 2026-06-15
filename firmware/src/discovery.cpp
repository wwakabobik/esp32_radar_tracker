#include "discovery.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiUdp.h>

#ifndef DISCOVERY_PORT
#define DISCOVERY_PORT 18832
#endif

static const char *DISCOVER_MAGIC = "PHUB_DISCOVER";
static const char *PREFS_NS = "phub";
static const char *KEY_MQTT_IP = "mqtt_ip";
static const char *KEY_MQTT_PORT = "mqtt_port";
static const char *KEY_OTA_IP = "ota_ip";
static const char *KEY_OTA_PORT = "ota_port";

bool HubDiscovery::loadCached(HubEndpoint &out) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) return false;
    String mqttIp = prefs.getString(KEY_MQTT_IP, "");
    uint16_t mqttPort = prefs.getUShort(KEY_MQTT_PORT, 0);
    String otaIp = prefs.getString(KEY_OTA_IP, "");
    uint16_t otaPort = prefs.getUShort(KEY_OTA_PORT, 0);
    prefs.end();

    if (mqttIp.isEmpty() || mqttPort == 0) return false;
    if (!out.mqttHost.fromString(mqttIp)) return false;
    out.mqttPort = mqttPort;
    if (!otaIp.isEmpty()) out.otaHost.fromString(otaIp);
    if (otaPort != 0) out.otaPort = otaPort;
    out.valid = true;
    return true;
}

void HubDiscovery::saveCached(const HubEndpoint &endpoint) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) return;
    prefs.putString(KEY_MQTT_IP, endpoint.mqttHost.toString());
    prefs.putUShort(KEY_MQTT_PORT, endpoint.mqttPort);
    prefs.putString(KEY_OTA_IP, endpoint.otaHost.toString());
    prefs.putUShort(KEY_OTA_PORT, endpoint.otaPort);
    prefs.end();
}

void HubDiscovery::clearCached() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) return;
    prefs.clear();
    prefs.end();
}

bool HubDiscovery::discover(HubEndpoint &out) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiUDP udp;
    if (!udp.begin(0)) {
        Serial.println("Discovery: UDP begin failed");
        return false;
    }

    const size_t magicLen = strlen(DISCOVER_MAGIC);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT)) continue;
        udp.write(reinterpret_cast<const uint8_t *>(DISCOVER_MAGIC), magicLen);
        udp.endPacket();
        delay(80);
    }

    Serial.printf("Discovery: broadcast on :%d\n", DISCOVERY_PORT);
    const unsigned long deadline = millis() + 4000;
    while (millis() < deadline) {
        const int packetSize = udp.parsePacket();
        if (packetSize <= 0) {
            delay(40);
            continue;
        }

        char buffer[256];
        const int len = udp.read(buffer, sizeof(buffer) - 1);
        if (len <= 0) continue;
        buffer[len] = '\0';

        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, buffer) != DeserializationError::Ok) continue;

        const char *mqttHost = doc["mqtt_host"] | "";
        const uint16_t mqttPort = doc["mqtt_port"] | 0;
        if (mqttHost[0] == '\0' || mqttPort == 0) continue;

        if (!out.mqttHost.fromString(mqttHost)) continue;
        out.mqttPort = mqttPort;

        const char *otaHost = doc["ota_host"] | mqttHost;
        out.otaPort = doc["ota_port"] | 18081;
        out.otaHost.fromString(otaHost);

        out.valid = true;
        Serial.printf("Discovery: hub at %s:%d\n", mqttHost, mqttPort);
        udp.stop();
        return true;
    }

    udp.stop();
    Serial.println("Discovery: no hub found");
    return false;
}
