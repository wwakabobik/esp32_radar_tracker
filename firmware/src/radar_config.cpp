#include "radar_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

RadarConfigStore gRadarConfig;

void RadarConfigStore::begin() {
    load(cfg_);
}

void RadarConfigStore::load(RadarConfig &out) {
    Preferences prefs;
    if (!prefs.begin("radar", true)) return;
    out.workMinCm = prefs.getUShort("w_min", 20);
    out.workMaxCm = prefs.getUShort("w_max", 400);
    out.workMode = prefs.getUChar("w_mode", 0);
    out.sleepBreathMaxCm = prefs.getUShort("s_max", 120);
    out.sleepEnergyMin = prefs.getUChar("s_nrg", 5);
    out.gateWork = prefs.getUChar("g_work", 6);
    out.gateSleep = prefs.getUChar("g_sleep", 8);
    prefs.end();
    cfg_ = out;
}

void RadarConfigStore::save(const RadarConfig &cfg) {
    cfg_ = cfg;
    Preferences prefs;
    if (!prefs.begin("radar", false)) return;
    prefs.putUShort("w_min", cfg.workMinCm);
    prefs.putUShort("w_max", cfg.workMaxCm);
    prefs.putUChar("w_mode", cfg.workMode);
    prefs.putUShort("s_max", cfg.sleepBreathMaxCm);
    prefs.putUChar("s_nrg", cfg.sleepEnergyMin);
    prefs.putUChar("g_work", cfg.gateWork);
    prefs.putUChar("g_sleep", cfg.gateSleep);
    prefs.end();
}

void RadarConfigStore::applyFromJson(const char *json) {
    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    RadarConfig cfg = cfg_;
    if (doc["work_presence_min_cm"].is<int>()) cfg.workMinCm = doc["work_presence_min_cm"];
    if (doc["work_presence_max_cm"].is<int>()) cfg.workMaxCm = doc["work_presence_max_cm"];
    if (doc["work_presence_type"].is<const char *>()) {
        const char *t = doc["work_presence_type"];
        if (strcmp(t, "moving") == 0) cfg.workMode = 1;
        else if (strcmp(t, "stationary") == 0) cfg.workMode = 2;
        else cfg.workMode = 0;
    }
    if (doc["sleep_breath_max_cm"].is<int>()) cfg.sleepBreathMaxCm = doc["sleep_breath_max_cm"];
    if (doc["sleep_energy_min"].is<int>()) cfg.sleepEnergyMin = doc["sleep_energy_min"];
    if (doc["radar_gate_work"].is<int>()) cfg.gateWork = doc["radar_gate_work"];
    if (doc["radar_gate_sleep"].is<int>()) cfg.gateSleep = doc["radar_gate_sleep"];
    if (doc["sleep_display_mode"].is<const char *>()) {
        // handled in main via display
    }
    save(cfg);
    Serial.println("Radar config updated");
}
