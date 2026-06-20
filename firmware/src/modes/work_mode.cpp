#include "modes/work_mode.h"

#include "event_log.h"
#include "radar_config.h"

static bool presenceMatchesLegacy(const RadarReading &reading) {
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
    fatigue_ = false;
    aiState_ = AiState::Vacant;
}

void WorkMode::onExit(Radar &) {}

void WorkMode::onRadar(const RadarReading &reading, const TinyMlResult *ai) {
    bool present = presenceMatchesLegacy(reading);
    fatigue_ = false;
    aiState_ = AiState::Vacant;

    if (ai && ai->confidence > 0) {
        aiState_ = ai->state;
        if (ai->state == AiState::EnvNoise) {
            return;
        }
        present = aiStateIsPresent(ai->state);
        fatigue_ = ai->state == AiState::StaticFatigue;
    }

    if (present == present_ && !fatigue_) return;
    const bool prevPresent = present_;
    present_ = present;

    if (prevPresent != present_ || fatigue_) {
        StaticJsonDocument<96> data;
        data["present"] = present_;
        data["fatigue"] = fatigue_;
        data["ai_state"] = static_cast<uint8_t>(aiState_);
        gEventLog.append("presence", "work", data.as<JsonObject>());
    }
}
