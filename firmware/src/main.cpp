#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <vector>
#include "buttons.h"
#include "config.h"
#include "display.h"
#include "modes/media_mode.h"
#include "modes/sleep_mode.h"
#include "modes/work_mode.h"
#include "mqtt_client.h"
#include "ota.h"
#include "radar.h"

static Radar radar;
static Display display;
static Buttons buttons;
static MqttHub mqtt;
static WorkMode workMode;
static SleepMode sleepMode;
static MediaMode mediaMode;

static String currentMode = "work";
static unsigned long lastRadarPublish = 0;

static void applyMode(const String &mode);
static void cycleMode();
static void handleDisplayMessage(const String &message);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Presence Hub boot");

    display.begin();
    radar.begin();
    buttons.begin([](const ButtonMessage &msg) {
        const char *event = msg.event == ButtonEvent::LongPress ? "long" : "press";
        mqtt.publishButton(msg.id, event);
        if (msg.id == 1 && msg.event == ButtonEvent::Press && currentMode == "work") {
            // reserved for pause/resume handled by daemon
        }
        if (msg.id == 2 && msg.event == ButtonEvent::Press) {
            cycleMode();
        }
    });

    mqtt.begin(
        [](const String &mode) { applyMode(mode); },
        [](const String &url) { Ota::performUpdate(url); },
        [](const String &message) { handleDisplayMessage(message); }
    );

    applyMode("work");
}

void loop() {
    mqtt.loop();
    buttons.loop();

    RadarReading reading{};
    if (radar.poll(reading)) {
        if (currentMode == "work") workMode.onRadar(reading);
        if (currentMode == "sleep") sleepMode.onRadar(reading);
        if (currentMode == "media") {
            mediaMode.onRadar(reading, [](const char *type, int value) {
                mqtt.publishGesture(type, value);
            });
        }

        const unsigned long now = millis();
        if (now - lastRadarPublish >= RADAR_PUBLISH_INTERVAL_MS) {
            mqtt.publishRadar(reading);
            lastRadarPublish = now;
        }
    }
}

static void applyMode(const String &mode) {
    if (currentMode == "work") workMode.onExit(radar);
    if (currentMode == "sleep") sleepMode.onExit(radar);
    if (currentMode == "media") mediaMode.onExit(radar);

    currentMode = mode;
    if (currentMode == "work") workMode.onEnter(radar);
    if (currentMode == "sleep") {
        sleepMode.onEnter(radar);
        display.setEnabled(false);
    } else {
        display.setEnabled(true);
    }
    if (currentMode == "media") mediaMode.onEnter(radar);

    mqtt.publishMode(currentMode.c_str());
    display.showBootMessage("Mode", currentMode.c_str());
}

static void cycleMode() {
    if (currentMode == "work") applyMode("sleep");
    else if (currentMode == "sleep") applyMode("media");
    else applyMode("work");
}

static void handleDisplayMessage(const String &message) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, message) != DeserializationError::Ok) return;
    if (!doc["widgets"].is<JsonArray>()) return;

    std::vector<DisplaySlot> slots;
    for (JsonObject item : doc["widgets"].as<JsonArray>()) {
        DisplaySlot slot;
        slot.pos = item["pos"] | 0;
        slot.text = item["text"] | "";
        slots.push_back(slot);
    }
    display.render(slots);
}
