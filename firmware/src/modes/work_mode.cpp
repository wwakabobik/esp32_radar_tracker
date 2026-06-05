#include "modes/work_mode.h"

void WorkMode::onEnter(Radar &radar) {
    radar.setHighSensitivity(false);
    present_ = false;
}

void WorkMode::onExit(Radar &) {}

void WorkMode::onRadar(const RadarReading &reading) {
    present_ = reading.present;
}
