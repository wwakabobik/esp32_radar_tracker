#include "mqtt_client.h"

#include <Preferences.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "config.h"
#include "discovery.h"
#include "button_config.h"
#include "event_log.h"
#include "pins.h"
#include "time_sync.h"

MqttHub *MqttHub::instance_ = nullptr;

static HubDiscovery discovery;
static HubEndpoint hubEndpoint;
static uint8_t mqttFailStreak_ = 0;
static unsigned long lastAnnouncementPollMs_ = 0;
static QueueHandle_t s_outQueue = nullptr;
static QueueHandle_t s_inQueue = nullptr;

static bool fallbackHostConfigured() {
    return MQTT_HOST[0] != '\0';
}

static bool applyFallbackHost(HubEndpoint &endpoint) {
    if (!fallbackHostConfigured()) return false;
    if (!endpoint.mqttHost.fromString(MQTT_HOST)) return false;
    endpoint.mqttPort = MQTT_PORT;
    endpoint.otaHost = endpoint.mqttHost;
    endpoint.otaPort = 18081;
    endpoint.valid = true;
    return true;
}

static bool resolveHubEndpointBoot(HubEndpoint &endpoint) {
    if (discovery.discover(endpoint)) {
        discovery.saveCached(endpoint);
        return true;
    }
    HubEndpoint cached;
    if (discovery.loadCached(cached)) {
        endpoint = cached;
        Serial.printf("Hub cache: %s:%d\n", endpoint.mqttHost.toString().c_str(), endpoint.mqttPort);
        return true;
    }
    return applyFallbackHost(endpoint);
}

static bool rediscoverHubEndpoint(HubEndpoint &endpoint) {
    discovery.clearCached();
    endpoint.valid = false;
    if (discovery.discover(endpoint)) {
        discovery.saveCached(endpoint);
        Serial.printf("Hub rediscovered: %s:%d\n", endpoint.mqttHost.toString().c_str(), endpoint.mqttPort);
        return true;
    }
    if (applyFallbackHost(endpoint)) {
        Serial.printf("Hub fallback: %s:%d\n", endpoint.mqttHost.toString().c_str(), endpoint.mqttPort);
        return true;
    }
    return false;
}

static unsigned long mqttRetryDelayMs() {
    if (mqttFailStreak_ <= 1) return MQTT_RETRY_MS;
    if (mqttFailStreak_ <= 5) return MQTT_RETRY_MS * 2;
    return MQTT_RETRY_MAX_MS;
}

void MqttHub::staticCallback(char *topic, byte *payload, unsigned int length) {
    if (instance_) instance_->onMessage(topic, payload, length);
}

void MqttHub::networkTask(void *arg) {
    auto *self = static_cast<MqttHub *>(arg);
    vTaskDelay(pdMS_TO_TICKS(300));
    for (;;) {
        self->networkTick();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void MqttHub::startNetworkTask() {
    if (!s_outQueue) s_outQueue = xQueueCreate(12, sizeof(OutboundMsg));
    if (!s_inQueue) s_inQueue = xQueueCreate(6, sizeof(InboundMsg));
    xTaskCreatePinnedToCore(networkTask, "mqtt_net", 10240, this, 1, nullptr, 0);
}

void MqttHub::loadHubAckId() {
    Preferences prefs;
    if (!prefs.begin("evlog", true)) return;
    hubAckId_ = prefs.getUInt("hub_ack", 0);
    prefs.end();
}

void MqttHub::saveHubAckId() {
    Preferences prefs;
    if (!prefs.begin("evlog", false)) return;
    prefs.putUInt("hub_ack", hubAckId_);
    prefs.end();
}

void MqttHub::setCurrentMode(const char *mode) {
    if (mode && mode[0]) currentMode_ = mode;
}

bool MqttHub::takeAutonomousNotify() {
    return autonomousNotify_.exchange(false);
}

void MqttHub::processInbound() {
    if (!s_inQueue) return;
    InboundMsg msg;
    while (xQueueReceive(s_inQueue, &msg, 0) == pdTRUE) {
        dispatchMessage(msg.topic, msg.payload);
    }
}

void MqttHub::begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler,
                    ConfigHandler configHandler, SyncAckHandler syncAckHandler,
                    AutonomousHandler autonomousHandler) {
    instance_ = this;
    modeHandler_ = modeHandler;
    otaHandler_ = otaHandler;
    displayHandler_ = displayHandler;
    configHandler_ = configHandler;
    syncAckHandler_ = syncAckHandler;
    autonomousHandler_ = autonomousHandler;
    hubOnline_.store(false);
    loadHubAckId();

    wifiClient_.setTimeout(MQTT_CONNECT_TIMEOUT_SEC);
    client_.setSocketTimeout(MQTT_CONNECT_TIMEOUT_SEC);
    client_.setKeepAlive(5);
    client_.setCallback(staticCallback);
    client_.setBufferSize(4096);

    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    Serial.printf("Connecting WiFi %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    startNetworkTask();
}

void MqttHub::ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    const unsigned long now = millis();
    if (lastWifiAttemptMs_ != 0 && now - lastWifiAttemptMs_ < 8000) return;
    lastWifiAttemptMs_ = now;
    Serial.printf("Connecting WiFi %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void MqttHub::subscribeTopics() {
    client_.subscribe(MQTT_TOPIC_MODE);
    client_.subscribe(MQTT_TOPIC_DISPLAY);
    client_.subscribe(MQTT_TOPIC_OTA);
    client_.subscribe(MQTT_TOPIC_SYNC_ACK);
    client_.subscribe(MQTT_TOPIC_CONFIG);
}

void MqttHub::ensureMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (client_.connected()) {
        mqttFailStreak_ = 0;
        return;
    }

    const unsigned long now = millis();
    const unsigned long retryMs = mqttRetryDelayMs();
    if (lastMqttAttemptMs_ != 0 && now - lastMqttAttemptMs_ < retryMs) return;
    lastMqttAttemptMs_ = now;

    if (now - lastNtpAttemptMs_ >= 60000) {
        lastNtpAttemptMs_ = now;
        TimeSync::trySync();
    }

    if (!hubEndpoint.valid || mqttFailStreak_ > 0) {
        if (mqttFailStreak_ > 0) {
            rediscoverHubEndpoint(hubEndpoint);
        } else {
            resolveHubEndpointBoot(hubEndpoint);
        }
    }
    if (!hubEndpoint.valid) return;

    client_.setServer(hubEndpoint.mqttHost, hubEndpoint.mqttPort);
    Serial.printf("Connecting MQTT %s:%d...\n", hubEndpoint.mqttHost.toString().c_str(), hubEndpoint.mqttPort);
    if (client_.connect(MQTT_CLIENT_ID)) {
        onHubConnected();
        mqttFailStreak_ = 0;
        lastSyncAttempt_ = 0;
        publishMode(currentMode_.c_str(), 0);
        subscribeTopics();
        publishStatus(currentMode_.c_str());
        flushSync();
        if (WiFi.status() == WL_CONNECTED && !TimeSync::ready()) TimeSync::ensure();
        Serial.printf("MQTT connected, pending events=%u\n", gEventLog.pendingAfter(hubAckId_));
    } else {
        mqttFailStreak_++;
        hubEndpoint.valid = false;
        Serial.printf("MQTT failed rc=%d (streak=%d)\n", client_.state(), mqttFailStreak_);
    }
}

void MqttHub::onHubConnected() {
    hubOnline_.store(true);
    publishFailStreak_ = 0;
    lastHubOkMs_ = millis();
}

void MqttHub::enterAutonomousMode() {
    const bool wasOnline = hubOnline_.load() || client_.connected();
    hubOnline_.store(false);
    publishFailStreak_ = 0;
    if (client_.connected()) client_.disconnect();
    wifiClient_.stop();
    if (wasOnline) {
        Serial.println("Autonomous mode — hub unreachable");
        autonomousNotify_.store(true);
    }
}

void MqttHub::markHubOk() {
    lastHubOkMs_ = millis();
    publishFailStreak_ = 0;
    hubOnline_.store(true);
}

void MqttHub::markPublishFailed() {
    const unsigned long now = millis();
    if (now - lastPublishFailMs_ > 2000) publishFailStreak_ = 0;
    lastPublishFailMs_ = now;
    publishFailStreak_++;
    if (publishFailStreak_ >= 1) enterAutonomousMode();
}

void MqttHub::enqueuePublish(const char *topic, const char *payload, bool retained) {
    if (!hubOnline_.load() || !topic || !payload || !s_outQueue) return;
    OutboundMsg msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.retained = retained;
    if (xQueueSend(s_outQueue, &msg, 0) != pdTRUE) {
        Serial.println("MQTT out queue full — drop");
    }
}

bool MqttHub::publishRaw(const char *topic, const char *payload, bool retained) {
    if (!hubOnline_.load() || !client_.connected()) return false;
    const bool ok = retained ? client_.publish(topic, payload, true) : client_.publish(topic, payload);
    if (!ok) {
        markPublishFailed();
        return false;
    }
    markHubOk();
    return true;
}

void MqttHub::flushSync() {
    lastSyncAttempt_ = 0;
    syncPendingEvents();
}

void MqttHub::syncPendingEvents() {
    if (!hubOnline_.load() || !client_.connected()) return;
    if (inflightToId_ > hubAckId_) return;
    if (gEventLog.pendingAfter(hubAckId_) == 0) return;

    StaticJsonDocument<4096> batch;
    JsonArray events = batch["events"].to<JsonArray>();
    uint32_t lastReadId = hubAckId_;
    if (!gEventLog.readBatch(hubAckId_, SYNC_BATCH_SIZE, events, lastReadId)) return;

    batch["from_ack"] = hubAckId_;
    batch["to_id"] = lastReadId;
    char payload[4096];
    const size_t len = serializeJson(batch, payload, sizeof(payload));
    if (len >= sizeof(payload)) return;
    if (publishRaw(MQTT_TOPIC_SYNC_EVENTS, payload)) {
        inflightToId_ = lastReadId;
    }
}

void MqttHub::handleSyncAck(const String &message) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, message) != DeserializationError::Ok) return;
    const uint32_t ackId = doc["ack_id"] | 0;
    if (ackId <= hubAckId_) return;

    hubAckId_ = ackId;
    inflightToId_ = 0;
    saveHubAckId();
    gEventLog.ackThrough(hubAckId_);
    if (syncAckHandler_) syncAckHandler_(ackId);
}

void MqttHub::networkTick() {
    if (WiFi.status() == WL_CONNECTED && !hubEndpoint.valid) {
        resolveHubEndpointBoot(hubEndpoint);
    }

    ensureWifi();

    if (client_.connected()) {
        if (!client_.loop()) enterAutonomousMode();
    } else if (hubOnline_.load()) {
        hubOnline_.store(false);
    } else if (WiFi.status() == WL_CONNECTED && !hubEndpoint.valid && !client_.connected()) {
        const unsigned long now = millis();
        if (now - lastAnnouncementPollMs_ >= 3000) {
            lastAnnouncementPollMs_ = now;
            HubEndpoint announced;
            if (discovery.pollAnnouncement(announced)) {
                hubEndpoint = announced;
                discovery.saveCached(hubEndpoint);
                Serial.printf("Hub announced: %s:%d\n", hubEndpoint.mqttHost.toString().c_str(),
                              hubEndpoint.mqttPort);
            }
        }
    }

    ensureMqtt();

    OutboundMsg out;
    while (hubOnline_.load() && s_outQueue && xQueueReceive(s_outQueue, &out, 0) == pdTRUE) {
        publishRaw(out.topic, out.payload, out.retained);
        if (!hubOnline_.load()) break;
    }

    if (!hubOnline_.load() || !client_.connected()) return;

    const unsigned long now = millis();
    if (now - lastHubOkMs_ > 4000 && now - lastProbeMs_ > 4000) {
        lastProbeMs_ = now;
        publishStatus(currentMode_.c_str());
    }
    if (now - lastSyncAttempt_ >= SYNC_INTERVAL_MS) {
        lastSyncAttempt_ = now;
        syncPendingEvents();
    }
    if (now - lastHeartbeat_ >= HEARTBEAT_INTERVAL_MS) {
        publishStatus(currentMode_.c_str());
        lastHeartbeat_ = now;
    }
}

void MqttHub::publishRadar(const RadarReading &reading, uint8_t aiState, uint8_t aiConfidence) {
    if (!hubOnline_.load()) return;
    StaticJsonDocument<512> doc;
    doc["dist"] = reading.dist;
    doc["gesture_dist"] = reading.dist;
    doc["presence_dist"] = reading.presence_dist;
    doc["m_dist"] = reading.m_dist;
    doc["s_dist"] = reading.s_dist;
    doc["s_energy"] = reading.s_energy;
    doc["m_energy"] = reading.m_energy;
    doc["gate_dist"] = reading.gate_dist;
    doc["m_gate_centroid"] = reading.m_gate_centroid;
    doc["presence"] = reading.present || reading.dist > 0;
    doc["moving"] = reading.moving;
    if (aiState != 255) {
        doc["ai_state"] = aiState;
        doc["ai_confidence"] = aiConfidence;
    }
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    char payload[512];
    if (serializeJson(doc, payload, sizeof(payload)) > 0) {
        enqueuePublish(MQTT_TOPIC_RADAR, payload);
    }
}

void MqttHub::publishRadarRaw(const RadarReading &reading) {
    if (!hubOnline_.load()) return;
    StaticJsonDocument<768> doc;
    doc["dist"] = reading.dist;
    doc["presence_dist"] = reading.presence_dist;
    doc["m_dist"] = reading.m_dist;
    doc["s_dist"] = reading.s_dist;
    doc["s_energy"] = reading.s_energy;
    doc["m_energy"] = reading.m_energy;
    doc["gate_dist"] = reading.gate_dist;
    doc["m_gate_centroid"] = reading.m_gate_centroid;
    doc["moving"] = reading.moving;
    doc["presence"] = reading.present;
    JsonArray mGates = doc["moving_gates"].to<JsonArray>();
    JsonArray sGates = doc["stationary_gates"].to<JsonArray>();
    for (uint8_t i = 0; i < RADAR_GATE_COUNT; ++i) {
        mGates.add(reading.moving_gates[i]);
        sGates.add(reading.stationary_gates[i]);
    }
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    char payload[768];
    if (serializeJson(doc, payload, sizeof(payload)) > 0) {
        enqueuePublish(MQTT_TOPIC_RADAR_RAW, payload);
    }
}

void MqttHub::publishAiState(const char *mode, const char *state, uint8_t confidence) {
    if (!hubOnline_.load() || !mode || !state) return;
    StaticJsonDocument<192> doc;
    doc["mode"] = mode;
    doc["state"] = state;
    doc["confidence"] = confidence;
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    char payload[192];
    serializeJson(doc, payload, sizeof(payload));
    enqueuePublish(MQTT_TOPIC_AI_STATE, payload);
}

void MqttHub::publishButton(uint8_t id, const char *event, uint32_t eventId) {
    if (!hubOnline_.load()) return;
    StaticJsonDocument<128> doc;
    doc["id"] = id;
    doc["event"] = event;
    if (eventId) doc["eid"] = eventId;
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    enqueuePublish(MQTT_TOPIC_BUTTON, payload);
}

void MqttHub::publishDebug(const char *kind, const char *payload) {
    if (!hubOnline_.load() || !kind || !payload) return;
    char topic[48];
    snprintf(topic, sizeof(topic), "%s/%s", MQTT_TOPIC_DEBUG, kind);
    enqueuePublish(topic, payload);
}

void MqttHub::publishGesture(const char *type, int value, uint32_t eventId) {
    if (!hubOnline_.load()) return;
    StaticJsonDocument<128> doc;
    doc["type"] = type;
    doc["value"] = value;
    if (eventId) doc["eid"] = eventId;
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    enqueuePublish(MQTT_TOPIC_GESTURE, payload);
}

void MqttHub::publishStatus(const char *mode) {
    if (!hubOnline_.load()) return;
    StaticJsonDocument<384> doc;
    doc["mode"] = mode;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["version"] = FIRMWARE_VERSION;
    doc["hub"] = hubEndpoint.mqttHost.toString();
    doc["pending_events"] = gEventLog.pendingAfter(hubAckId_);
    doc["buffered"] = gEventLog.count();
    if (TimeSync::ready()) doc["time_synced"] = true;
    const auto &btn = gButtonConfig.current();
    doc["btn1_pin"] = btn.pin1;
    doc["btn2_pin"] = btn.pin2;
    doc["btn1_lvl"] = digitalRead(btn.pin1);
    doc["btn2_lvl"] = digitalRead(btn.pin2);
    doc["btn_active_low"] = btn.activeLow;
    char payload[384];
    serializeJson(doc, payload, sizeof(payload));
    publishRaw(MQTT_TOPIC_STATUS, payload);
}

void MqttHub::publishMode(const char *mode, uint32_t eventId) {
    currentMode_ = mode;
    if (!hubOnline_.load()) return;
    StaticJsonDocument<128> doc;
    doc["mode"] = mode;
    if (eventId) doc["eid"] = eventId;
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    enqueuePublish(MQTT_TOPIC_MODE, payload, true);
}

void MqttHub::onMessage(char *topic, byte *payload, unsigned int length) {
    if (!s_inQueue || !topic || !payload || length == 0) return;
    if (length >= sizeof(InboundMsg::payload)) length = sizeof(InboundMsg::payload) - 1;

    InboundMsg msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    memcpy(msg.payload, payload, length);
    msg.payload[length] = '\0';
    xQueueSend(s_inQueue, &msg, 0);
}

void MqttHub::dispatchMessage(const char *topic, const char *payload) {
    if (!topic || !payload) return;
    const String message = payload;

    if (strcmp(topic, MQTT_TOPIC_MODE) == 0) {
        StaticJsonDocument<128> doc;
        String mode = message;
        if (deserializeJson(doc, message) == DeserializationError::Ok && doc["mode"].is<const char *>()) {
            mode = doc["mode"].as<const char *>();
        }
        if (mode != currentMode_) publishMode(currentMode_.c_str(), 0);
        markHubOk();
        if (modeHandler_) modeHandler_(mode);
        return;
    }
    if (strcmp(topic, MQTT_TOPIC_DISPLAY) == 0) {
        markHubOk();
        if (displayHandler_) displayHandler_(message);
        return;
    }
    if (strcmp(topic, MQTT_TOPIC_OTA) == 0) {
        markHubOk();
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, message) == DeserializationError::Ok && doc["url"].is<const char *>()) {
            if (otaHandler_) otaHandler_(doc["url"].as<const char *>());
        }
        return;
    }
    if (strcmp(topic, MQTT_TOPIC_SYNC_ACK) == 0) {
        markHubOk();
        handleSyncAck(message);
        return;
    }
    if (strcmp(topic, MQTT_TOPIC_CONFIG) == 0) {
        markHubOk();
        if (configHandler_) configHandler_(message);
    }
}
