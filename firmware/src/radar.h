#pragma once

#include <ArduinoJson.h>

struct RadarReading {
    bool present = false;
    bool moving = false;
    bool stationary = false;
    uint16_t dist = 0;  // gesture distance (close range)
    uint16_t presence_dist = 0;  // presence / body distance (farther target ok)
    uint16_t gate_dist = 0;  // nearest gate with energy (fine mode)
    uint8_t s_energy = 0;
    uint8_t m_energy = 0;
    uint16_t s_dist = 0;
    uint16_t m_dist = 0;
    uint8_t m_gate_centroid = 0;
};

// Use dist computed in Radar::poll(); fall back to m/s if unset.
inline uint16_t radarNearestCm(const RadarReading &r, uint8_t minEnergy = 15) {
    if (r.dist > 0) return r.dist;
    uint16_t best = UINT16_MAX;
    if (r.m_dist > 0 && r.m_energy >= minEnergy) best = min(best, r.m_dist);
    if (r.s_dist > 0 && r.s_energy >= minEnergy) best = min(best, r.s_dist);
    if (best != UINT16_MAX) return best;
    if (r.gate_dist > 0) return r.gate_dist;
    return 0;
}

class Radar {
public:
    bool begin();
    bool poll(RadarReading &out);
    void setHighSensitivity(bool enabled);
    void setGestureProfile(bool enabled);
    bool isReady() const { return ready_; }

private:
    bool ready_ = false;
    bool highSensitivity_ = false;
    bool gestureProfile_ = false;

    void applyGateLimits();
    bool configureGestureSensor();
    bool restoreWorkSensor();
};
