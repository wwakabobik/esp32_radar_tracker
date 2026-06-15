#include "gesture_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

GestureConfigStore gGestureConfig;

void GestureConfigStore::begin() {
    load(cfg_);
}

void GestureConfigStore::load(GestureConfig &out) {
    Preferences prefs;
    if (!prefs.begin("gesture", true)) return;
    out.zoneMinCm = prefs.getUShort("z_min", 12);
    out.zoneMaxCm = prefs.getUShort("z_max", 28);
    out.holdMs = prefs.getUShort("hold", 400);
    out.debounceMs = prefs.getUShort("deb", 1200);
    out.debug = prefs.getBool("debug", false);
    prefs.end();
    cfg_ = out;
}

void GestureConfigStore::save(const GestureConfig &cfg) {
    cfg_ = cfg;
    Preferences prefs;
    if (!prefs.begin("gesture", false)) return;
    prefs.putUShort("z_min", cfg.zoneMinCm);
    prefs.putUShort("z_max", cfg.zoneMaxCm);
    prefs.putUShort("hold", cfg.holdMs);
    prefs.putUShort("deb", cfg.debounceMs);
    prefs.putBool("debug", cfg.debug);
    prefs.end();
}

void GestureConfigStore::applyFromJson(const char *json) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    GestureConfig cfg = cfg_;
    if (doc["gesture_zone_min_cm"].is<int>()) cfg.zoneMinCm = doc["gesture_zone_min_cm"];
    if (doc["gesture_zone_max_cm"].is<int>()) cfg.zoneMaxCm = doc["gesture_zone_max_cm"];
    if (doc["gesture_hold_ms"].is<int>()) cfg.holdMs = doc["gesture_hold_ms"];
    if (doc["gesture_debounce_ms"].is<int>()) cfg.debounceMs = doc["gesture_debounce_ms"];
    else if (doc["gesture_swipe_debounce_ms"].is<int>()) cfg.debounceMs = doc["gesture_swipe_debounce_ms"];
    if (doc["gesture_debug"].is<bool>()) cfg.debug = doc["gesture_debug"].as<bool>();
    else if (doc["gesture_debug"].is<int>()) cfg.debug = doc["gesture_debug"].as<int>() != 0;
    save(cfg);
    Serial.println("Gesture config updated");
}
