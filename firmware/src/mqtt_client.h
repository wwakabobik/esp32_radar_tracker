#pragma once

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <atomic>
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
    using SyncAckHandler = std::function<void(uint32_t ackId)>;
    using AutonomousHandler = std::function<void()>;

    void begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler,
               ConfigHandler configHandler = nullptr, SyncAckHandler syncAckHandler = nullptr,
               AutonomousHandler autonomousHandler = nullptr);
    void processInbound();
    bool takeAutonomousNotify();
    bool connected() { return client_.connected(); }
    bool hubOnline() const { return hubOnline_.load(); }
    void setCurrentMode(const char *mode);
    const String &currentMode() const { return currentMode_; }
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
    struct OutboundMsg {
        char topic[48];
        char payload[768];
        bool retained = false;
    };

    struct InboundMsg {
        char topic[48];
        char payload[1024];
    };

    WiFiClient wifiClient_;
    PubSubClient client_{wifiClient_};
    ModeHandler modeHandler_;
    OtaHandler otaHandler_;
    DisplayHandler displayHandler_;
    ConfigHandler configHandler_;
    SyncAckHandler syncAckHandler_;
    AutonomousHandler autonomousHandler_;
    std::atomic<bool> hubOnline_{false};
    std::atomic<bool> autonomousNotify_{false};
    uint8_t publishFailStreak_ = 0;
    unsigned long lastHubOkMs_ = 0;
    unsigned long lastPublishFailMs_ = 0;
    unsigned long lastHeartbeat_ = 0;
    unsigned long lastSyncAttempt_ = 0;
    unsigned long lastMqttAttemptMs_ = 0;
    unsigned long lastWifiAttemptMs_ = 0;
    unsigned long lastNtpAttemptMs_ = 0;
    unsigned long lastProbeMs_ = 0;
    uint32_t hubAckId_ = 0;
    uint32_t inflightToId_ = 0;
    String currentMode_ = "work";

    void networkTick();
    void startNetworkTask();
    void ensureWifi();
    void ensureMqtt();
    void subscribeTopics();
    void syncPendingEvents();
    void handleSyncAck(const String &message);
    void loadHubAckId();
    void saveHubAckId();
    void dispatchMessage(const char *topic, const char *payload);
    void onMessage(char *topic, byte *payload, unsigned int length);
    void onHubConnected();
    void enterAutonomousMode();
    void markHubOk();
    void markPublishFailed();
    bool publishRaw(const char *topic, const char *payload, bool retained = false);
    void enqueuePublish(const char *topic, const char *payload, bool retained = false);
    static void staticCallback(char *topic, byte *payload, unsigned int length);
    static void networkTask(void *arg);
    static MqttHub *instance_;
};
