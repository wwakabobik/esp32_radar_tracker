#include "mqtt_client.h"
#include "config.h"

MqttHub *MqttHub::instance_ = nullptr;

void MqttHub::staticCallback(char *topic, byte *payload, unsigned int length) {
    if (instance_) instance_->onMessage(topic, payload, length);
}

void MqttHub::begin(ModeHandler modeHandler, OtaHandler otaHandler, DisplayHandler displayHandler) {
    instance_ = this;
    modeHandler_ = modeHandler;
    otaHandler_ = otaHandler;
    displayHandler_ = displayHandler;
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    ensureWifi();
    client_.setServer(MQTT_HOST, MQTT_PORT);
    client_.setCallback(staticCallback);
    client_.setBufferSize(1024);
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

void MqttHub::ensureMqtt() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (client_.connected()) return;
    Serial.printf("Connecting MQTT %s:%d...\n", MQTT_HOST, MQTT_PORT);
    if (client_.connect(MQTT_CLIENT_ID)) {
        client_.subscribe(MQTT_TOPIC_MODE);
        client_.subscribe(MQTT_TOPIC_DISPLAY);
        client_.subscribe(MQTT_TOPIC_OTA);
        publishStatus(currentMode_.c_str());
        Serial.println("MQTT connected");
    } else {
        Serial.printf("MQTT failed rc=%d\n", client_.state());
    }
}

void MqttHub::loop() {
    ensureWifi();
    ensureMqtt();
    client_.loop();
    const unsigned long now = millis();
    if (client_.connected() && now - lastHeartbeat_ >= HEARTBEAT_INTERVAL_MS) {
        publishStatus(currentMode_.c_str());
        lastHeartbeat_ = now;
    }
}

void MqttHub::publishRadar(const RadarReading &reading) {
    if (!client_.connected()) return;
    StaticJsonDocument<192> doc;
    doc["dist"] = reading.dist;
    doc["s_energy"] = reading.s_energy;
    doc["m_energy"] = reading.m_energy;
    doc["s_dist"] = reading.s_dist;
    doc["m_dist"] = reading.m_dist;
    doc["presence"] = reading.present;
    doc["ts"] = millis();
    char payload[192];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_RADAR, payload);
}

void MqttHub::publishButton(uint8_t id, const char *event) {
    if (!client_.connected()) return;
    StaticJsonDocument<96> doc;
    doc["id"] = id;
    doc["event"] = event;
    char payload[96];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_BUTTON, payload);
}

void MqttHub::publishGesture(const char *type, int value) {
    if (!client_.connected()) return;
    StaticJsonDocument<96> doc;
    doc["type"] = type;
    doc["value"] = value;
    char payload[96];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_GESTURE, payload);
}

void MqttHub::publishStatus(const char *mode) {
    if (!client_.connected()) return;
    StaticJsonDocument<192> doc;
    doc["mode"] = mode;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    char payload[192];
    serializeJson(doc, payload, sizeof(payload));
    client_.publish(MQTT_TOPIC_STATUS, payload);
}

void MqttHub::publishMode(const char *mode) {
    currentMode_ = mode;
    if (!client_.connected()) return;
    client_.publish(MQTT_TOPIC_MODE, mode, true);
}

void MqttHub::onMessage(char *topic, byte *payload, unsigned int length) {
    String message;
    message.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) message += static_cast<char>(payload[i]);

    if (String(topic) == MQTT_TOPIC_MODE) {
        currentMode_ = message;
        if (modeHandler_) modeHandler_(message);
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
}
