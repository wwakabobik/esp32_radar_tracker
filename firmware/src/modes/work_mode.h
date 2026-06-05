#pragma once

#include "radar.h"

class WorkMode {
public:
    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading);
    bool isPresent() const { return present_; }

private:
    bool present_ = false;
};
