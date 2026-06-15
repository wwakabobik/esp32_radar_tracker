#pragma once

#include "radar.h"

class SleepMode {
public:
    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading);
    void manualSleepStart();
    void manualWake();
    bool breathingDetected() const { return breathing_; }
    bool isAsleep() const { return asleep_; }
    int movements() const { return movements_; }

private:
    bool breathing_ = false;
    bool asleep_ = false;
    uint8_t stableCount_ = 0;
    int breathingStable_ = 0;
    int movements_ = 0;
};
