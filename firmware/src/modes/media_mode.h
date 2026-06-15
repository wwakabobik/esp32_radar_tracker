#pragma once

#include <functional>
#include "radar.h"

class MediaMode {
public:
    using GestureCallback = std::function<void(const char *type, int value)>;
    using DebugCallback = std::function<void(const char *payload)>;

    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading, GestureCallback cb, DebugCallback debugCb = nullptr);

private:
    unsigned long zoneEnterMs_ = 0;
    unsigned long lastNextMs_ = 0;
    unsigned long lastDebugMs_ = 0;
    bool zoneArmed_ = true;

    uint16_t gestureDist(const RadarReading &reading) const;
    bool inNearZone(uint16_t d) const;
    void publishDebug(const RadarReading &reading, DebugCallback debugCb);
};
