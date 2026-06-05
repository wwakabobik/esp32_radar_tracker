#pragma once

#include <functional>
#include "radar.h"

class MediaMode {
public:
    using GestureCallback = std::function<void(const char *type, int value)>;
    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading, GestureCallback cb);

private:
    uint16_t lastDist_ = 0;
    unsigned long lastMoveMs_ = 0;
    bool hasBaseline_ = false;
};
