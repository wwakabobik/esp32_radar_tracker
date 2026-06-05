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

    void begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler);
    void loop();
    bool connected() const { return client_.connected(); }
    void publishRadar(const RadarReading &reading);
    void publishButton(uint8_t id, const char *event);
    void publishGesture(const char *type, int value);
    void publishStatus(const char *mode);
    void publishMode(const char *mode);

private:
    WiFiClient wifiClient_;
    PubSubClient client_{wifiClient_};
    ModeHandler modeHandler_;
    OtaHandler otaHandler_;
    DisplayHandler displayHandler_;
    unsigned long lastHeartbeat_ = 0;
    String currentMode_ = "work";

    void ensureWifi();
    void ensureMqtt();
    void onMessage(char *topic, byte *payload, unsigned int length);
    static void staticCallback(char *topic, byte *payload, unsigned int length);
    static MqttHub *instance_;
};
