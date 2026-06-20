#pragma once

#include <Arduino.h>

struct AiConfig {
    bool enabled = true;
    bool recordMode = false;
    uint8_t confidenceMin = 60;
    uint16_t fatigueMinutes = 45;
    bool fallbackHeuristics = true;
};

class AiConfigStore {
public:
    void begin();
    void load(AiConfig &out);
    void save(const AiConfig &cfg);
    void applyFromJson(const char *json);
    const AiConfig &current() const { return cfg_; }

private:
    AiConfig cfg_;
};

extern AiConfigStore gAiConfig;
