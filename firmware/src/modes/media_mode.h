#pragma once

#include <functional>
#include "radar.h"
#include "tiny_ml.h"

class MediaMode {
public:
    using GestureCallback = std::function<void(const char *type, int value)>;
    using DebugCallback = std::function<void(const char *payload)>;

    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading, GestureCallback cb, DebugCallback debugCb = nullptr,
                 const TinyMlResult *ai = nullptr);

private:
    unsigned long zoneEnterMs_ = 0;
    unsigned long lastNextMs_ = 0;
    unsigned long lastPrevMs_ = 0;
    unsigned long lastHoverMs_ = 0;
    unsigned long lastDebugMs_ = 0;
    bool zoneArmed_ = true;

    uint16_t gestureDist(const RadarReading &reading) const;
    bool inNearZone(uint16_t d) const;
    bool mlZoneOk(uint16_t d) const;
    void publishDebug(const RadarReading &reading, DebugCallback debugCb, const TinyMlResult *ai);
    void fallbackZoneHold(const RadarReading &reading, GestureCallback cb);
    void handleMlGesture(const TinyMlResult &ai, const RadarReading &reading, GestureCallback cb);
};
