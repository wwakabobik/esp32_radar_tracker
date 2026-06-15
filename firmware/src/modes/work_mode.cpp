#include "modes/work_mode.h"

#include "event_log.h"
#include "radar_config.h"

static bool presenceMatches(const RadarReading &reading) {
    const auto &cfg = gRadarConfig.current();
    const uint16_t dist = reading.dist;
    if (dist < cfg.workMinCm || dist > cfg.workMaxCm) return false;
    if (cfg.workMode == 1) return reading.moving;
    if (cfg.workMode == 2) return reading.stationary;
    return reading.present;
}

void WorkMode::onEnter(Radar &radar) {
    radar.setHighSensitivity(false);
    present_ = false;
}

void WorkMode::onExit(Radar &) {}

void WorkMode::onRadar(const RadarReading &reading) {
    const bool present = presenceMatches(reading);
    if (present == present_) return;
    present_ = present;

    StaticJsonDocument<64> data;
    data["present"] = present_;
    gEventLog.append("presence", "work", data.as<JsonObject>());
}
