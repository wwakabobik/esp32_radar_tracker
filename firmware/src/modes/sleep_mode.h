#pragma once

#include "radar.h"
#include "tiny_ml.h"

class SleepMode {
public:
    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading, const TinyMlResult *ai = nullptr);
    void manualSleepStart();
    void manualWake();
    bool breathingDetected() const { return breathing_; }
    bool isAsleep() const { return asleep_; }
    int movements() const { return movements_; }
    AiState aiState() const { return aiState_; }

private:
    bool breathing_ = false;
    bool asleep_ = false;
    uint8_t stableCount_ = 0;
    int breathingStable_ = 0;
    int movements_ = 0;
    AiState aiState_ = AiState::SleepAbsent;
    AiState lastAiState_ = AiState::SleepAbsent;
};
