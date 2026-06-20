#pragma once

#include "feature_buffer.h"
#include "model_data.h"
#include "radar.h"

enum class AiState : uint8_t {
    Vacant = 0,
    ActiveFocus = 1,
    StaticFatigue = 2,
    EnvNoise = 3,
    GestureNone = 20,
    GestureNext = 21,
    GesturePrev = 22,
    GestureHover = 23,
    SleepAbsent = 30,
    SleepBreathingStable = 31,
    SleepRestless = 32,
};

struct TinyMlResult {
    AiState state = AiState::Vacant;
    uint8_t confidence = 0;  // 0-100
    uint8_t classId = 0;

    TinyMlResult() = default;
    TinyMlResult(AiState s, uint8_t conf, uint8_t cls)
        : state(s), confidence(conf), classId(cls) {}
};

struct TinyMlFeatures {
    float values[TINYML_FEATURE_COUNT]{};
};

class TinyMlEngine {
public:
    void begin();
    void pushFrame(const RadarReading &reading, unsigned long nowMs);
    void setMode(const char *mode);
    TinyMlResult inferWork();
    TinyMlResult inferGesture();
    TinyMlResult inferSleep();
    const FeatureBuffer &buffer() const { return buffer_; }
    bool extractFeatures(TinyMlFeatures &out) const;
    const char *stateName(AiState state) const;

private:
    FeatureBuffer buffer_;
    String mode_ = "work";
    unsigned long staticSinceMs_ = 0;
    AiState lastWorkState_ = AiState::Vacant;

    TinyMlResult runMlp(const int8_t *weights, const int8_t *bias, uint8_t inputCount,
                        uint8_t classCount, AiState baseState) const;
    TinyMlResult heuristicWork() const;
    TinyMlResult heuristicGesture() const;
    TinyMlResult heuristicSleep() const;
};

extern TinyMlEngine gTinyMl;

inline bool aiStateIsPresent(AiState state) {
    return state == AiState::ActiveFocus || state == AiState::StaticFatigue;
}

inline const char *aiStateToString(AiState state) {
    switch (state) {
        case AiState::Vacant:
            return "vacant";
        case AiState::ActiveFocus:
            return "active_focus";
        case AiState::StaticFatigue:
            return "static_fatigue";
        case AiState::EnvNoise:
            return "env_noise";
        case AiState::GestureNone:
            return "gesture_none";
        case AiState::GestureNext:
            return "gesture_next";
        case AiState::GesturePrev:
            return "gesture_prev";
        case AiState::GestureHover:
            return "gesture_hover";
        case AiState::SleepAbsent:
            return "sleep_absent";
        case AiState::SleepBreathingStable:
            return "sleep_breathing_stable";
        case AiState::SleepRestless:
            return "sleep_restless";
        default:
            return "unknown";
    }
}
