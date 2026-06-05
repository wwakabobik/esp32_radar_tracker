#pragma once

#include "radar.h"

class SleepMode {
public:
    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading);
    bool breathingDetected() const { return breathing_; }

private:
    bool breathing_ = false;
    uint8_t stableCount_ = 0;
};
