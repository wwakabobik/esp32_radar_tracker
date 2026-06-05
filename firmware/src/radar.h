#pragma once

#include <ArduinoJson.h>

struct RadarReading {
    bool present = false;
    uint16_t dist = 0;
    uint8_t s_energy = 0;
    uint8_t m_energy = 0;
    uint16_t s_dist = 0;
    uint16_t m_dist = 0;
};

class Radar {
public:
    bool begin();
    bool poll(RadarReading &out);
    void setHighSensitivity(bool enabled);
    bool isReady() const { return ready_; }

private:
    bool ready_ = false;
    bool highSensitivity_ = false;
};
