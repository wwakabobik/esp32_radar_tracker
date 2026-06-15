#include "gpio_probe.h"

#include <ArduinoJson.h>

#include "button_config.h"
#include "mqtt_client.h"

const uint8_t GpioProbe::kCandidatePins[] = {2,  4,  5,  12, 13, 14, 15, 18, 19,
                                             25, 26, 27, 32, 33, 34, 35, 36, 39};
const size_t GpioProbe::kPinCount = sizeof(kCandidatePins) / sizeof(kCandidatePins[0]);

uint8_t GpioProbe::readPin(uint8_t pin) const {
    return digitalRead(pin) ? 1 : 0;
}

void GpioProbe::begin() {
    for (uint8_t pin : kCandidatePins) {
        if (pin >= 34 && pin <= 39) pinMode(pin, INPUT);
        else pinMode(pin, INPUT_PULLUP);
    }
    for (size_t i = 0; i < kPinCount; ++i) {
        lastState_[i] = readPin(kCandidatePins[i]);
    }
    initialized_ = true;
    lastHeartbeatMs_ = millis();
}

void GpioProbe::loop(MqttHub &mqtt) {
    const auto &cfg = gButtonConfig.current();
    if (!cfg.gpioProbe && !cfg.buttonLearn) return;
    if (!mqtt.connected()) return;

    for (uint8_t pin : kCandidatePins) {
        if (pin >= 34 && pin <= 39) pinMode(pin, INPUT);
        else pinMode(pin, INPUT_PULLUP);
    }

    const unsigned long now = millis();
    const bool heartbeat = (now - lastHeartbeatMs_ >= 5000);

    bool anyChange = false;
    uint8_t current[kPinCount];
    for (size_t i = 0; i < kPinCount; ++i) {
        current[i] = readPin(kCandidatePins[i]);
        if (!initialized_ || current[i] != lastState_[i]) anyChange = true;
    }
    if (!anyChange && !(heartbeat && cfg.gpioProbe)) return;

    if (cfg.buttonLearn) {
        for (size_t i = 0; i < kPinCount; ++i) {
            if (initialized_ && current[i] == lastState_[i]) continue;
            const uint8_t pin = kCandidatePins[i];
            StaticJsonDocument<128> ev;
            ev["pin"] = pin;
            ev["level"] = current[i];
            ev["edge"] = (current[i] < lastState_[i]) ? "falling" : "rising";
            char buf[128];
            serializeJson(ev, buf, sizeof(buf));
            mqtt.publishDebug("press", buf);
            Serial.printf("GPIO %u -> %u\n", pin, current[i]);
        }
    }

    if (!cfg.gpioProbe) {
        for (size_t i = 0; i < kPinCount; ++i) lastState_[i] = current[i];
        initialized_ = true;
        return;
    }

    StaticJsonDocument<512> doc;
    JsonObject pins = doc["pins"].to<JsonObject>();
    JsonArray changed = doc["changed"].to<JsonArray>();
    for (size_t i = 0; i < kPinCount; ++i) {
        const uint8_t pin = kCandidatePins[i];
        pins[String(pin)] = current[i];
        if (!initialized_ || current[i] != lastState_[i]) changed.add(pin);
        lastState_[i] = current[i];
    }
    doc["heartbeat"] = heartbeat;
    doc["btn1"] = gButtonConfig.current().pin1;
    doc["btn2"] = gButtonConfig.current().pin2;
    initialized_ = true;
    if (heartbeat) lastHeartbeatMs_ = now;

    char payload[512];
    serializeJson(doc, payload, sizeof(payload));
    mqtt.publishDebug("gpio", payload);
}
