#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <vector>
#include "buttons.h"
#include "config.h"
#include "display.h"
#include "event_log.h"
#include "gpio_probe.h"
#include "mode_store.h"
#include "modes/media_mode.h"
#include "modes/sleep_mode.h"
#include "modes/work_mode.h"
#include "mqtt_client.h"
#include "ota.h"
#include "pins.h"
#include "radar.h"
#include "radar_config.h"
#include "button_config.h"
#include "gesture_config.h"
#include "time_sync.h"

static Radar radar;
static Display display;
static Buttons buttons;
static GpioProbe gpioProbe;
static MqttHub mqtt;
static WorkMode workMode;
static SleepMode sleepMode;
static MediaMode mediaMode;

static String currentMode = "work";
static String sleepDisplayMode = "off";
static unsigned long lastRadarPublish = 0;
static unsigned long lastNextGestureMs = 0;

static void applyMode(const String &mode, bool fromHub = false);
static void cycleMode();
static void handleDisplayMessage(const String &message);
static void handleConfigMessage(const String &message);
static void logModeChange(const String &mode);
static void handleButton(const ButtonMessage &msg);
static void applySleepDisplay();

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Presence Hub boot");
    Serial.printf("FW %s | radar UART %d/%d | I2C %d/%d | BTN %d/%d\n",
                  FIRMWARE_VERSION, PIN_RADAR_RX, PIN_RADAR_TX,
                  PIN_I2C_SDA, PIN_I2C_SCL, PIN_BTN_1, PIN_BTN_2);

    ModeStore::begin();
    gEventLog.begin();
    gRadarConfig.begin();
    gButtonConfig.begin();
    gGestureConfig.begin();
    display.begin();
    radar.begin();
    buttons.begin(handleButton);

    mqtt.begin(
        [](const String &mode) { applyMode(mode, true); },
        [](const String &url) { Ota::performUpdate(url); },
        [](const String &message) { handleDisplayMessage(message); },
        [](const String &message) { handleConfigMessage(message); }
    );

    applyMode(ModeStore::load(), false);
}

static void handleRadarReading(const RadarReading &reading) {
    if (currentMode == "work" || currentMode == "media") workMode.onRadar(reading);
    if (currentMode == "sleep") sleepMode.onRadar(reading);
    if (currentMode == "media") {
        mediaMode.onRadar(
            reading,
            [](const char *type, int value) {
                const unsigned long now = millis();
                const GestureConfig &cfg = gGestureConfig.current();
                if (strcmp(type, "next") == 0 && now - lastNextGestureMs < cfg.debounceMs) return;
                lastNextGestureMs = now;
                StaticJsonDocument<64> data;
                data["type"] = type;
                data["value"] = value;
                const uint32_t eid = gEventLog.append("gesture", "media", data.as<JsonObject>());
                mqtt.publishGesture(type, value, eid);
            },
            [](const char *payload) { mqtt.publishDebug("gesture", payload); });
    }

    const unsigned long now = millis();
    const unsigned long publishMs =
        currentMode == "media" ? RADAR_PUBLISH_INTERVAL_MEDIA_MS : RADAR_PUBLISH_INTERVAL_MS;
    if (now - lastRadarPublish >= publishMs) {
        mqtt.publishRadar(reading);
        lastRadarPublish = now;
    }
}

void loop() {
    mqtt.loop();
    buttons.loop();
    if (gButtonConfig.current().gpioProbe) gpioProbe.loop(mqtt);
    display.loop();

    const int pollLimit = currentMode == "media" ? MEDIA_RADAR_POLLS : 1;
    for (int i = 0; i < pollLimit; ++i) {
        RadarReading reading{};
        if (!radar.poll(reading)) break;
        handleRadarReading(reading);
    }
}

static void applySleepDisplay() {
    display.setSleepMode(sleepDisplayMode.c_str());
    if (currentMode == "sleep") {
        if (sleepDisplayMode == "off") {
            display.setEnabled(false);
        } else {
            display.setEnabled(true);
        }
    }
}

static void handleConfigMessage(const String &message) {
    gRadarConfig.applyFromJson(message.c_str());
    gGestureConfig.applyFromJson(message.c_str());
    gButtonConfig.applyFromJson(message.c_str());
    buttons.applyConfig();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, message) == DeserializationError::Ok) {
        if (doc["sleep_display_mode"].is<const char *>()) {
            sleepDisplayMode = doc["sleep_display_mode"].as<const char *>();
            applySleepDisplay();
        }
    }
    radar.setHighSensitivity(currentMode == "sleep");
    radar.setGestureProfile(currentMode == "media");
}

static void logModeChange(const String &mode) {
    StaticJsonDocument<64> data;
    data["mode"] = mode;
    const uint32_t eid = gEventLog.append("mode", mode.c_str(), data.as<JsonObject>());
    mqtt.publishMode(mode.c_str(), eid);
}

static void applyMode(const String &mode, bool fromHub) {
    if (mode != "work" && mode != "sleep" && mode != "media") return;
    display.setModeLetter(mode.c_str());
    if (mode == currentMode) return;

    if (currentMode == "work") workMode.onExit(radar);
    if (currentMode == "sleep") sleepMode.onExit(radar);
    if (currentMode == "media") mediaMode.onExit(radar);

    currentMode = mode;
    ModeStore::save(currentMode);

    if (currentMode == "work") workMode.onEnter(radar);
    if (currentMode == "sleep") sleepMode.onEnter(radar);
    if (currentMode == "media") mediaMode.onEnter(radar);

    applySleepDisplay();
    if (currentMode != "sleep") display.setEnabled(true);

    radar.setHighSensitivity(currentMode == "sleep");
    radar.setGestureProfile(currentMode == "media");

    if (!fromHub) {
        logModeChange(currentMode);
        display.showOverlay("Mode", currentMode.c_str());
    } else {
        mqtt.publishMode(currentMode.c_str(), 0);
        display.setModeLetter(currentMode.c_str());
    }
}

static void cycleMode() {
    if (currentMode == "work") applyMode("sleep", false);
    else if (currentMode == "sleep") applyMode("media", false);
    else applyMode("work", false);
}

static void handleButton(const ButtonMessage &msg) {
    const char *event = msg.event == ButtonEvent::LongPress ? "long" : "press";

    if (currentMode == "sleep") {
        if (msg.id == 1 && msg.event == ButtonEvent::Press) {
            sleepMode.manualSleepStart();
            StaticJsonDocument<64> data;
            data["id"] = 1;
            data["event"] = "press";
            const uint32_t eid = gEventLog.append("button", "sleep", data.as<JsonObject>());
            mqtt.publishButton(msg.id, event, eid);
            display.showOverlay("Sleep", "Recording");
            return;
        }
        if (msg.id == 2 && msg.event == ButtonEvent::Press) {
            sleepMode.manualWake();
            StaticJsonDocument<64> data;
            data["id"] = 2;
            data["event"] = "press";
            const uint32_t eid = gEventLog.append("button", "sleep", data.as<JsonObject>());
            mqtt.publishButton(msg.id, event, eid);
            applyMode("media", false);
            return;
        }
    }

    if ((currentMode == "work" || currentMode == "media") && msg.id == 1) {
        StaticJsonDocument<64> data;
        data["id"] = msg.id;
        data["event"] = event;
        const uint32_t eid = gEventLog.append("button", currentMode.c_str(), data.as<JsonObject>());
        mqtt.publishButton(msg.id, event, eid);
        if (msg.event == ButtonEvent::LongPress) {
            display.showOverlay("Session", "Reset");
        }
        return;
    }

    if (msg.id == 2 && msg.event == ButtonEvent::Press) {
        cycleMode();
        return;
    }

    StaticJsonDocument<64> data;
    data["id"] = msg.id;
    data["event"] = event;
    const uint32_t eid = gEventLog.append("button", currentMode.c_str(), data.as<JsonObject>());
    mqtt.publishButton(msg.id, event, eid);
}

static void handleDisplayMessage(const String &message) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, message) != DeserializationError::Ok) return;
    if (!doc["widgets"].is<JsonArray>()) return;

    if (doc["sleep_display_mode"].is<const char *>()) {
        sleepDisplayMode = doc["sleep_display_mode"].as<const char *>();
        applySleepDisplay();
    }

    const uint8_t brightness = doc["brightness"] | 0;
    std::vector<DisplaySlot> slots;
    for (JsonObject item : doc["widgets"].as<JsonArray>()) {
        DisplaySlot slot;
        slot.pos = item["pos"] | 0;
        slot.text = item["text"] | "";
        slot.font = item["font"] | "medium";
        slot.scroll = item["scroll"] | false;
        if (slot.text.length()) slots.push_back(slot);
    }
    display.render(slots, brightness, doc["line_count"] | 2);
}
