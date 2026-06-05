#include "modes/media_mode.h"

void MediaMode::onEnter(Radar &) {
    lastDist_ = 0;
    lastMoveMs_ = millis();
    hasBaseline_ = false;
}

void MediaMode::onExit(Radar &) {}

void MediaMode::onRadar(const RadarReading &reading, GestureCallback cb) {
    if (!reading.present) {
        hasBaseline_ = false;
        return;
    }
    const unsigned long now = millis();
    if (!hasBaseline_) {
        lastDist_ = reading.dist;
        hasBaseline_ = true;
        lastMoveMs_ = now;
        return;
    }

    const int delta = static_cast<int>(reading.dist) - static_cast<int>(lastDist_);
    if (abs(delta) >= 15 && (now - lastMoveMs_) > 400) {
        if (delta < 0 && cb) cb("next", abs(delta));
        if (delta > 0 && cb) cb("prev", abs(delta));
        lastMoveMs_ = now;
    }

    if (reading.dist >= 20 && reading.dist <= 100 && cb) {
        const int vol = map(reading.dist, 20, 100, 100, 0);
        cb("vol", vol);
    }
    lastDist_ = reading.dist;
}
