#include "mqtt_client.h"

#include <Preferences.h>

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

static bool resolveHubEndpointCached(HubEndpoint &endpoint) {
    if (endpoint.valid) return true;

    HubEndpoint cached;
    if (discovery.loadCached(cached)) {
        endpoint = cached;
        Serial.printf("Hub cache: %s:%d\n", endpoint.mqttHost.toString().c_str(), endpoint.mqttPort);
        return true;
    }

    if (applyFallbackHost(endpoint)) {
        Serial.printf("Hub fallback: %s:%d\n", endpoint.mqttHost.toString().c_str(), endpoint.mqttPort);
        return true;
    }

    endpoint.valid = false;
    return false;
}

static bool resolveHubEndpointBoot(HubEndpoint &endpoint) {
    if (resolveHubEndpointCached(endpoint)) return true;

    if (discovery.discover(endpoint)) {
        discovery.saveCached(endpoint);
        return true;
    }

    return applyFallbackHost(endpoint);
}

void MqttHub::staticCallback(char *topic, byte *payload, unsigned int length) {
    if (instance_) instance_->onMessage(topic, payload, length);
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

void MqttHub::begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler,
                    ConfigHandler configHandler, SyncAckHandler syncAckHandler) {
    instance_ = this;
    modeHandler_ = modeHandler;
    otaHandler_ = otaHandler;
    displayHandler_ = displayHandler;
    configHandler_ = configHandler;
    syncAckHandler_ = syncAckHandler;
    loadHubAckId();

    wifiClient_.setTimeout(MQTT_CONNECT_TIMEOUT_SEC);
    client_.setSocketTimeout(MQTT_CONNECT_TIMEOUT_SEC);
    client_.setKeepAlive(30);
    client_.setCallback(staticCallback);
    client_.setBufferSize(4096);

    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    Serial.printf("Connecting WiFi %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
        TimeSync::ensure();
        resolveHubEndpointBoot(hubEndpoint);
    } else {
        Serial.println("WiFi failed — continuing offline");
    }

    ensureMqtt();
}

void MqttHub::ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    const unsigned long now = millis();
    if (lastWifiAttemptMs_ != 0 && now - lastWifiAttemptMs_ < 5000) return;
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

unsigned long mqttRetryDelayMs() {
    if (mqttFailStreak_ <= 1) return MQTT_RETRY_MS;
    if (mqttFailStreak_ <= 5) return MQTT_RETRY_MS * 2;
    return MQTT_RETRY_MAX_MS;
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

    if (!resolveHubEndpointCached(hubEndpoint)) {
        Serial.println("Hub endpoint unknown — skip MQTT (no cache/fallback)");
        return;
    }

    client_.setServer(hubEndpoint.mqttHost, hubEndpoint.mqttPort);
    Serial.printf("Connecting MQTT %s:%d...\n", hubEndpoint.mqttHost.toString().c_str(), hubEndpoint.mqttPort);
    if (client_.connect(MQTT_CLIENT_ID)) {
        mqttFailStreak_ = 0;
        lastSyncAttempt_ = 0;
        publishMode(currentMode_.c_str(), 0);
        subscribeTopics();
        publishStatus(currentMode_.c_str());
        flushSync();
        Serial.printf("MQTT connected, pending events=%u\n", gEventLog.pendingAfter(hubAckId_));
    } else {
        mqttFailStreak_++;
        Serial.printf("MQTT failed rc=%d (streak=%d, retry=%lums)\n", client_.state(), mqttFailStreak_,
                      retryMs);
    }
}

void MqttHub::flushSync() {
    lastSyncAttempt_ = 0;
    syncPendingEvents();
}

void MqttHub::syncPendingEvents() {
    if (!client_.connected()) return;
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
    if (len >= sizeof(payload)) {
        Serial.println("Sync batch too large");
        return;
    }
    if (client_.publish(MQTT_TOPIC_SYNC_EVENTS, payload)) {
        inflightToId_ = lastReadId;
        Serial.printf("Sync published %u events (%u..%u)\n", events.size(), hubAckId_ + 1, lastReadId);
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
    Serial.printf("Sync ack through %u, pending=%u\n", hubAckId_, gEventLog.pendingAfter(hubAckId_));
    if (syncAckHandler_) syncAckHandler_(ackId);
}

void MqttHub::loop() {
    ensureWifi();
    client_.loop();
    ensureMqtt();

    const unsigned long now = millis();
    if (!client_.connected()) return;

    if (now - lastNtpAttemptMs_ >= 60000) {
        lastNtpAttemptMs_ = now;
        TimeSync::trySync();
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
    if (!client_.connected()) return;
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
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n > 0) client_.publish(MQTT_TOPIC_RADAR, payload);
}

void MqttHub::publishRadarRaw(const RadarReading &reading) {
    if (!client_.connected()) return;
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
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n > 0) client_.publish(MQTT_TOPIC_RADAR_RAW, payload);
}

void MqttHub::publishAiState(const char *mode, const char *state, uint8_t confidence) {
    if (!client_.connected() || !mode || !state) return;
    StaticJsonDocument<192> doc;
    doc["mode"] = mode;
    doc["state"] = state;
    doc["confidence"] = confidence;
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    char payload[192];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_AI_STATE, payload);
}

void MqttHub::publishButton(uint8_t id, const char *event, uint32_t eventId) {
    if (!client_.connected()) return;
    StaticJsonDocument<128> doc;
    doc["id"] = id;
    doc["event"] = event;
    if (eventId) doc["eid"] = eventId;
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_BUTTON, payload);
}

void MqttHub::publishDebug(const char *kind, const char *payload) {
    if (!client_.connected() || !kind || !payload) return;
    char topic[48];
    snprintf(topic, sizeof(topic), "%s/%s", MQTT_TOPIC_DEBUG, kind);
    const bool ok = client_.publish(topic, payload);
    if (!ok) Serial.printf("MQTT debug publish failed (%s)\n", topic);
}

void MqttHub::publishGesture(const char *type, int value, uint32_t eventId) {
    if (!client_.connected()) return;
    StaticJsonDocument<128> doc;
    doc["type"] = type;
    doc["value"] = value;
    if (eventId) doc["eid"] = eventId;
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_GESTURE, payload);
}

void MqttHub::publishStatus(const char *mode) {
    if (!client_.connected()) return;
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
    client_.publish(MQTT_TOPIC_STATUS, payload);
}

void MqttHub::publishMode(const char *mode, uint32_t eventId) {
    currentMode_ = mode;
    if (!client_.connected()) return;
    StaticJsonDocument<128> doc;
    doc["mode"] = mode;
    if (eventId) doc["eid"] = eventId;
    char payload[128];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_MODE, payload, true);
}

void MqttHub::onMessage(char *topic, byte *payload, unsigned int length) {
    String message;
    message.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) message += static_cast<char>(payload[i]);

    if (String(topic) == MQTT_TOPIC_MODE) {
        StaticJsonDocument<128> doc;
        String mode = message;
        if (deserializeJson(doc, message) == DeserializationError::Ok && doc["mode"].is<const char *>()) {
            mode = doc["mode"].as<const char *>();
        }
        if (mode != currentMode_) {
            publishMode(currentMode_.c_str(), 0);
        }
        if (modeHandler_) modeHandler_(mode);
        return;
    }
    if (String(topic) == MQTT_TOPIC_DISPLAY) {
        if (displayHandler_) displayHandler_(message);
        return;
    }
    if (String(topic) == MQTT_TOPIC_OTA) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, message) == DeserializationError::Ok && doc["url"].is<const char *>()) {
            if (otaHandler_) otaHandler_(doc["url"].as<const char *>());
        }
        return;
    }
    if (String(topic) == MQTT_TOPIC_SYNC_ACK) {
        handleSyncAck(message);
        return;
    }
    if (String(topic) == MQTT_TOPIC_CONFIG) {
        if (configHandler_) configHandler_(message);
    }
}
