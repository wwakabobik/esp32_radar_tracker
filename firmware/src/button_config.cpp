#include "button_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

ButtonConfigStore gButtonConfig;

void ButtonConfigStore::begin() {
    load(cfg_);
}

void ButtonConfigStore::load(ButtonHwConfig &out) {
    Preferences prefs;
    if (!prefs.begin("buttons", true)) return;
    out.pin1 = prefs.getUChar("pin1", 18);
    out.pin2 = prefs.getUChar("pin2", 5);
    out.activeLow = prefs.getBool("active_low", true);
    out.gpioProbe = prefs.getBool("gpio_probe", false);
    out.buttonLearn = prefs.getBool("btn_learn", false);
    prefs.end();
    cfg_ = out;
}

void ButtonConfigStore::save(const ButtonHwConfig &cfg) {
    cfg_ = cfg;
    Preferences prefs;
    if (!prefs.begin("buttons", false)) return;
    prefs.putUChar("pin1", cfg.pin1);
    prefs.putUChar("pin2", cfg.pin2);
    prefs.putBool("active_low", cfg.activeLow);
    prefs.putBool("gpio_probe", cfg.gpioProbe);
    prefs.putBool("btn_learn", cfg.buttonLearn);
    prefs.end();
}

void ButtonConfigStore::applyFromJson(const char *json) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    ButtonHwConfig cfg = cfg_;
    if (doc["btn1_pin"].is<int>()) cfg.pin1 = doc["btn1_pin"];
    if (doc["btn2_pin"].is<int>()) cfg.pin2 = doc["btn2_pin"];
    if (doc["btn_active_low"].is<bool>()) cfg.activeLow = doc["btn_active_low"];
    if (doc["button_gpio_probe"].is<bool>()) cfg.gpioProbe = doc["button_gpio_probe"];
    if (doc["button_learn"].is<bool>()) cfg.buttonLearn = doc["button_learn"];
    save(cfg);
    Serial.printf("Button config: pins %u/%u active_low=%d probe=%d\n", cfg.pin1, cfg.pin2,
                  cfg.activeLow, cfg.gpioProbe);
}
