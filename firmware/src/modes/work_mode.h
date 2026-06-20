#pragma once

#include "radar.h"
#include "tiny_ml.h"

class WorkMode {
public:
    void onEnter(Radar &radar);
    void onExit(Radar &radar);
    void onRadar(const RadarReading &reading, const TinyMlResult *ai = nullptr);
    bool isPresent() const { return present_; }
    bool isFatigue() const { return fatigue_; }
    AiState aiState() const { return aiState_; }

private:
    bool present_ = false;
    bool fatigue_ = false;
    AiState aiState_ = AiState::Vacant;
};
