#pragma once

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <functional>
#include <vector>
#include "display.h"
#include "radar.h"

class MqttHub {
public:
    using ModeHandler = std::function<void(const String &)>;
    using OtaHandler = std::function<void(const String &)>;
    using DisplayHandler = std::function<void(const String &)>;
    using ConfigHandler = std::function<void(const String &)>;

    void begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler,
               ConfigHandler configHandler = nullptr);
    void loop();
    bool connected() { return client_.connected(); }
    void publishRadar(const RadarReading &reading, uint8_t aiState = 255, uint8_t aiConfidence = 0);
    void publishRadarRaw(const RadarReading &reading);
    void publishAiState(const char *mode, const char *state, uint8_t confidence);
    void publishButton(uint8_t id, const char *event, uint32_t eventId = 0);
    void publishGesture(const char *type, int value, uint32_t eventId = 0);
    void publishStatus(const char *mode);
    void publishMode(const char *mode, uint32_t eventId = 0);
    void publishDebug(const char *kind, const char *payload);
    void flushSync();

private:
    WiFiClient wifiClient_;
    PubSubClient client_{wifiClient_};
    ModeHandler modeHandler_;
    OtaHandler otaHandler_;
    DisplayHandler displayHandler_;
    ConfigHandler configHandler_;
    unsigned long lastHeartbeat_ = 0;
    unsigned long lastSyncAttempt_ = 0;
    uint32_t hubAckId_ = 0;
    uint32_t inflightToId_ = 0;
    String currentMode_ = "work";

    void ensureWifi();
    void ensureMqtt();
    void ensureTime();
    void syncPendingEvents();
    void handleSyncAck(const String &message);
    void loadHubAckId();
    void saveHubAckId();
    void onMessage(char *topic, byte *payload, unsigned int length);
    static void staticCallback(char *topic, byte *payload, unsigned int length);
    static MqttHub *instance_;
};
