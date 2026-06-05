#include "modes/sleep_mode.h"

void SleepMode::onEnter(Radar &radar) {
    radar.setHighSensitivity(true);
    breathing_ = false;
    stableCount_ = 0;
}

void SleepMode::onExit(Radar &radar) {
    radar.setHighSensitivity(false);
}

void SleepMode::onRadar(const RadarReading &reading) {
    if (reading.present && reading.s_energy > 5 && reading.s_dist > 0 && reading.s_dist < 120) {
        if (++stableCount_ >= 5) breathing_ = true;
    } else {
        stableCount_ = 0;
        breathing_ = false;
    }
}
