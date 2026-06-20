#pragma once

#include <Arduino.h>

struct GestureConfig {
    uint16_t zoneMinCm = 12;
    uint16_t zoneMaxCm = 28;
    uint16_t holdMs = 400;
    uint16_t debounceMs = 2500;
    bool debug = false;
    bool zoneHoldEnabled = true;
    bool mlNextEnabled = false;
    bool mlPrevEnabled = false;
    bool mlVolEnabled = false;
    bool mlRequireZone = true;
    uint16_t mlVolMinCm = 12;
    uint16_t mlVolMaxCm = 40;
    uint16_t mlVolIntervalMs = 2500;
};

class GestureConfigStore {
public:
    void begin();
    void load(GestureConfig &out);
    void save(const GestureConfig &cfg);
    void applyFromJson(const char *json);
    const GestureConfig &current() const { return cfg_; }

private:
    GestureConfig cfg_;
};

extern GestureConfigStore gGestureConfig;
