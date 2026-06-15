#pragma once

#include <Arduino.h>

struct RadarConfig {
    uint16_t workMinCm = 20;
    uint16_t workMaxCm = 400;
    uint8_t workMode = 0;
    uint16_t sleepBreathMaxCm = 120;
    uint8_t sleepEnergyMin = 5;
    uint8_t gateWork = 6;
    uint8_t gateSleep = 8;
};

class RadarConfigStore {
public:
    void begin();
    void load(RadarConfig &out);
    void save(const RadarConfig &cfg);
    void applyFromJson(const char *json);
    const RadarConfig &current() const { return cfg_; }

private:
    RadarConfig cfg_;
};

extern RadarConfigStore gRadarConfig;
