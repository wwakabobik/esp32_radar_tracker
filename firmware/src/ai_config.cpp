#include "ai_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

AiConfigStore gAiConfig;

void AiConfigStore::begin() {
    load(cfg_);
}

void AiConfigStore::load(AiConfig &out) {
    Preferences prefs;
    if (!prefs.begin("ai", true)) return;
    out.enabled = prefs.getBool("enabled", true);
    out.recordMode = prefs.getBool("record", false);
    out.confidenceMin = prefs.getUChar("conf_min", 60);
    out.fatigueMinutes = prefs.getUShort("fatigue_min", 45);
    out.fallbackHeuristics = prefs.getBool("fallback", true);
    prefs.end();
}

void AiConfigStore::save(const AiConfig &cfg) {
    Preferences prefs;
    if (!prefs.begin("ai", false)) return;
    prefs.putBool("enabled", cfg.enabled);
    prefs.putBool("record", cfg.recordMode);
    prefs.putUChar("conf_min", cfg.confidenceMin);
    prefs.putUShort("fatigue_min", cfg.fatigueMinutes);
    prefs.putBool("fallback", cfg.fallbackHeuristics);
    prefs.end();
    cfg_ = cfg;
}

void AiConfigStore::applyFromJson(const char *json) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    AiConfig next = cfg_;
    if (doc["ai_enabled"].is<bool>()) next.enabled = doc["ai_enabled"].as<bool>();
    if (doc["ai_record_mode"].is<bool>()) next.recordMode = doc["ai_record_mode"].as<bool>();
    if (doc["ai_confidence_min"].is<uint8_t>()) {
        next.confidenceMin = doc["ai_confidence_min"].as<uint8_t>();
    } else if (doc["ai_confidence_min"].is<int>()) {
        next.confidenceMin = static_cast<uint8_t>(doc["ai_confidence_min"].as<int>());
    }
    if (doc["ai_fatigue_minutes"].is<uint16_t>()) {
        next.fatigueMinutes = doc["ai_fatigue_minutes"].as<uint16_t>();
    } else if (doc["ai_fatigue_minutes"].is<int>()) {
        next.fatigueMinutes = static_cast<uint16_t>(doc["ai_fatigue_minutes"].as<int>());
    }
    if (doc["ai_fallback_heuristics"].is<bool>()) {
        next.fallbackHeuristics = doc["ai_fallback_heuristics"].as<bool>();
    }
    save(next);
}
