#include "gesture_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

GestureConfigStore gGestureConfig;

static bool jsonBool(const JsonVariant &v, bool fallback) {
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int>()) return v.as<int>() != 0;
    return fallback;
}

void GestureConfigStore::begin() {
    load(cfg_);
}

void GestureConfigStore::load(GestureConfig &out) {
    Preferences prefs;
    if (!prefs.begin("gesture", true)) return;
    out.zoneMinCm = prefs.getUShort("z_min", 12);
    out.zoneMaxCm = prefs.getUShort("z_max", 28);
    out.holdMs = prefs.getUShort("hold", 400);
    out.debounceMs = prefs.getUShort("deb", 2500);
    out.debug = prefs.getBool("debug", false);
    out.zoneHoldEnabled = prefs.getBool("z_hold", true);
    out.mlNextEnabled = prefs.getBool("ml_next", false);
    out.mlPrevEnabled = prefs.getBool("ml_prev", false);
    out.mlVolEnabled = prefs.getBool("ml_vol", false);
    out.mlRequireZone = prefs.getBool("ml_in_z", true);
    out.mlVolMinCm = prefs.getUShort("vol_min", 12);
    out.mlVolMaxCm = prefs.getUShort("vol_max", 40);
    out.mlVolIntervalMs = prefs.getUShort("vol_ms", 2500);
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
    prefs.putBool("z_hold", cfg.zoneHoldEnabled);
    prefs.putBool("ml_next", cfg.mlNextEnabled);
    prefs.putBool("ml_prev", cfg.mlPrevEnabled);
    prefs.putBool("ml_vol", cfg.mlVolEnabled);
    prefs.putBool("ml_in_z", cfg.mlRequireZone);
    prefs.putUShort("vol_min", cfg.mlVolMinCm);
    prefs.putUShort("vol_max", cfg.mlVolMaxCm);
    prefs.putUShort("vol_ms", cfg.mlVolIntervalMs);
    prefs.end();
}

void GestureConfigStore::applyFromJson(const char *json) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    GestureConfig cfg = cfg_;
    if (doc["gesture_zone_min_cm"].is<int>()) cfg.zoneMinCm = doc["gesture_zone_min_cm"];
    if (doc["gesture_zone_max_cm"].is<int>()) cfg.zoneMaxCm = doc["gesture_zone_max_cm"];
    if (doc["gesture_hold_ms"].is<int>()) cfg.holdMs = doc["gesture_hold_ms"];
    if (doc["gesture_debounce_ms"].is<int>()) cfg.debounceMs = doc["gesture_debounce_ms"];
    else if (doc["gesture_swipe_debounce_ms"].is<int>()) cfg.debounceMs = doc["gesture_swipe_debounce_ms"];
    if (!doc["gesture_debug"].isNull()) cfg.debug = jsonBool(doc["gesture_debug"], cfg.debug);
    if (!doc["gesture_zone_hold"].isNull()) cfg.zoneHoldEnabled = jsonBool(doc["gesture_zone_hold"], cfg.zoneHoldEnabled);
    if (!doc["gesture_ml_next"].isNull()) cfg.mlNextEnabled = jsonBool(doc["gesture_ml_next"], cfg.mlNextEnabled);
    if (!doc["gesture_ml_prev"].isNull()) cfg.mlPrevEnabled = jsonBool(doc["gesture_ml_prev"], cfg.mlPrevEnabled);
    if (!doc["gesture_ml_vol"].isNull()) cfg.mlVolEnabled = jsonBool(doc["gesture_ml_vol"], cfg.mlVolEnabled);
    if (!doc["gesture_ml_in_zone"].isNull()) cfg.mlRequireZone = jsonBool(doc["gesture_ml_in_zone"], cfg.mlRequireZone);
    if (doc["gesture_ml_vol_min_cm"].is<int>()) cfg.mlVolMinCm = doc["gesture_ml_vol_min_cm"];
    if (doc["gesture_ml_vol_max_cm"].is<int>()) cfg.mlVolMaxCm = doc["gesture_ml_vol_max_cm"];
    if (doc["gesture_ml_vol_ms"].is<int>()) cfg.mlVolIntervalMs = doc["gesture_ml_vol_ms"];
    save(cfg);
    Serial.println("Gesture config updated");
}
