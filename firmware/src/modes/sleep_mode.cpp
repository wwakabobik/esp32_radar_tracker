#include "modes/sleep_mode.h"

#include "event_log.h"
#include "radar_config.h"

void SleepMode::onEnter(Radar &radar) {
    radar.setHighSensitivity(true);
    breathing_ = false;
    stableCount_ = 0;
    breathingStable_ = 0;
    asleep_ = false;
    movements_ = 0;
}

void SleepMode::onExit(Radar &radar) {
    radar.setHighSensitivity(false);
    if (asleep_) {
        gEventLog.append("sleep_end", "sleep");
        asleep_ = false;
    }
}

void SleepMode::manualSleepStart() {
    if (asleep_) return;
    asleep_ = true;
    breathingStable_ = 5;
    gEventLog.append("sleep_start", "sleep");
}

void SleepMode::manualWake() {
    if (!asleep_) return;
    asleep_ = false;
    breathingStable_ = 0;
    gEventLog.append("sleep_end", "sleep");
}

void SleepMode::onRadar(const RadarReading &reading) {
    const auto &cfg = gRadarConfig.current();

    if (!reading.present) {
        breathingStable_ = max(0, breathingStable_ - 1);
        stableCount_ = 0;
        breathing_ = false;
        if (breathingStable_ == 0 && asleep_) {
            movements_++;
            gEventLog.append("sleep_movement", "sleep");
        }
        return;
    }

    if (reading.s_energy > cfg.sleepEnergyMin && reading.s_dist > 0 &&
        reading.s_dist < cfg.sleepBreathMaxCm) {
        breathingStable_++;
        if (++stableCount_ >= 5) breathing_ = true;
    } else {
        breathingStable_ = max(0, breathingStable_ - 1);
        stableCount_ = 0;
        breathing_ = false;
    }

    if (breathingStable_ >= 5 && !asleep_) {
        asleep_ = true;
        gEventLog.append("sleep_start", "sleep");
    }
}
