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
static unsigned long lastDiscoveryAttempt_ = 0;
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

static bool resolveHubEndpoint(bool forceDiscover) {
    if (!forceDiscover && hubEndpoint.valid) return true;

    HubEndpoint cached;
    if (!forceDiscover && discovery.loadCached(cached)) {
        hubEndpoint = cached;
        Serial.printf("Hub cache: %s:%d\n", hubEndpoint.mqttHost.toString().c_str(), hubEndpoint.mqttPort);
        return true;
    }

    if (discovery.discover(hubEndpoint)) {
        discovery.saveCached(hubEndpoint);
        return true;
    }

    if (applyFallbackHost(hubEndpoint)) {
        Serial.printf("Hub fallback: %s:%d\n", hubEndpoint.mqttHost.toString().c_str(), hubEndpoint.mqttPort);
        return true;
    }

    hubEndpoint.valid = false;
    return false;
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

void MqttHub::begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler,
                    ConfigHandler configHandler) {
    instance_ = this;
    modeHandler_ = modeHandler;
    otaHandler_ = otaHandler;
    displayHandler_ = displayHandler;
    configHandler_ = configHandler;
    loadHubAckId();
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    ensureWifi();
    client_.setCallback(staticCallback);
    client_.setBufferSize(4096);
    ensureMqtt();
}

void MqttHub::ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.printf("Connecting WiFi %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi failed");
    }
}

void MqttHub::ensureTime() {
    if (WiFi.status() == WL_CONNECTED) TimeSync::ensure();
}

void MqttHub::ensureMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (client_.connected()) {
        mqttFailStreak_ = 0;
        return;
    }

    ensureTime();

    const unsigned long now = millis();
    const bool forceDiscover = mqttFailStreak_ >= 3;
    if (!hubEndpoint.valid || forceDiscover) {
        if (forceDiscover || now - lastDiscoveryAttempt_ > 5000) {
            lastDiscoveryAttempt_ = now;
            if (!resolveHubEndpoint(forceDiscover)) {
                Serial.println("Hub not found (discovery)");
                return;
            }
        } else if (!hubEndpoint.valid) {
            return;
        }
    }

    client_.setServer(hubEndpoint.mqttHost, hubEndpoint.mqttPort);
    Serial.printf("Connecting MQTT %s:%d...\n", hubEndpoint.mqttHost.toString().c_str(), hubEndpoint.mqttPort);
    if (client_.connect(MQTT_CLIENT_ID)) {
        client_.subscribe(MQTT_TOPIC_MODE);
        client_.subscribe(MQTT_TOPIC_DISPLAY);
        client_.subscribe(MQTT_TOPIC_OTA);
        client_.subscribe(MQTT_TOPIC_SYNC_ACK);
        client_.subscribe(MQTT_TOPIC_CONFIG);
        publishStatus(currentMode_.c_str());
        mqttFailStreak_ = 0;
        lastSyncAttempt_ = 0;
        Serial.printf("MQTT connected, pending events=%u\n", gEventLog.pendingAfter(hubAckId_));
    } else {
        mqttFailStreak_++;
        Serial.printf("MQTT failed rc=%d (streak=%d)\n", client_.state(), mqttFailStreak_);
        if (mqttFailStreak_ >= 3) {
            hubEndpoint.valid = false;
            discovery.clearCached();
        }
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
}

void MqttHub::loop() {
    ensureWifi();
    ensureMqtt();
    ensureTime();
    client_.loop();

    const unsigned long now = millis();
    if (client_.connected()) {
        if (now - lastSyncAttempt_ >= SYNC_INTERVAL_MS) {
            lastSyncAttempt_ = now;
            syncPendingEvents();
        }
        if (now - lastHeartbeat_ >= HEARTBEAT_INTERVAL_MS) {
            publishStatus(currentMode_.c_str());
            lastHeartbeat_ = now;
        }
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
        currentMode_ = mode;
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
