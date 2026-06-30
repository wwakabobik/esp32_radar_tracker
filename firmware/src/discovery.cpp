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

static IPAddress subnetBroadcast() {
    const IPAddress ip = WiFi.localIP();
    const IPAddress mask = WiFi.subnetMask();
    return IPAddress(
        static_cast<uint8_t>(ip[0] | (~mask[0] & 0xFF)),
        static_cast<uint8_t>(ip[1] | (~mask[1] & 0xFF)),
        static_cast<uint8_t>(ip[2] | (~mask[2] & 0xFF)),
        static_cast<uint8_t>(ip[3] | (~mask[3] & 0xFF)));
}

static bool parseDiscoveryReply(const char *buffer, HubEndpoint &out) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, buffer) != DeserializationError::Ok) return false;

    const char *mqttHost = doc["mqtt_host"] | "";
    const uint16_t mqttPort = doc["mqtt_port"] | 0;
    if (mqttHost[0] == '\0' || mqttPort == 0) return false;

    if (!out.mqttHost.fromString(mqttHost)) return false;
    out.mqttPort = mqttPort;

    const char *otaHost = doc["ota_host"] | mqttHost;
    out.otaPort = doc["ota_port"] | 18081;
    out.otaHost.fromString(otaHost);

    out.valid = true;
    Serial.printf("Discovery: hub at %s:%d\n", mqttHost, mqttPort);
    return true;
}

static bool readDiscoveryPacket(WiFiUDP &udp, HubEndpoint &out) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) return false;

    char buffer[256];
    const int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len <= 0) return false;
    buffer[len] = '\0';
    return parseDiscoveryReply(buffer, out);
}

bool HubDiscovery::discover(HubEndpoint &out) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiUDP udp;
    if (!udp.begin(0)) {
        Serial.println("Discovery: UDP begin failed");
        return false;
    }

    const size_t magicLen = strlen(DISCOVER_MAGIC);
    const IPAddress targets[] = {subnetBroadcast(), IPAddress(255, 255, 255, 255)};
    for (int attempt = 0; attempt < 4; ++attempt) {
        for (const IPAddress &target : targets) {
            if (!udp.beginPacket(target, DISCOVERY_PORT)) continue;
            udp.write(reinterpret_cast<const uint8_t *>(DISCOVER_MAGIC), magicLen);
            udp.endPacket();
        }
        delay(60);
        if (readDiscoveryPacket(udp, out)) {
            udp.stop();
            return true;
        }
    }

    Serial.printf("Discovery: broadcast on :%d (subnet %s)\n", DISCOVERY_PORT,
                  subnetBroadcast().toString().c_str());
    const unsigned long deadline = millis() + 5000;
    while (millis() < deadline) {
        if (readDiscoveryPacket(udp, out)) {
            udp.stop();
            return true;
        }
        delay(40);
    }

    udp.stop();
    Serial.println("Discovery: no hub found");
    return false;
}

bool HubDiscovery::pollAnnouncement(HubEndpoint &out) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiUDP udp;
    if (!udp.begin(DISCOVERY_PORT)) return false;

    bool found = false;
    const unsigned long deadline = millis() + 40;
    while (millis() < deadline) {
        if (readDiscoveryPacket(udp, out)) {
            found = true;
            break;
        }
        delay(5);
    }
    udp.stop();
    return found;
}
