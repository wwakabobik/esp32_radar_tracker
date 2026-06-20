#pragma once

#include "radar.h"

static constexpr uint8_t FEATURE_BUFFER_SIZE = 32;

struct FeatureFrame {
    RadarReading reading;
    unsigned long millis = 0;
};

class FeatureBuffer {
public:
    void push(const RadarReading &reading, unsigned long nowMs);
    uint8_t count() const { return count_; }
    bool full() const { return count_ >= FEATURE_BUFFER_SIZE; }
    const FeatureFrame &at(uint8_t index) const;
    const FeatureFrame &newest() const;
    const FeatureFrame &oldest() const;
    void clear();

private:
    FeatureFrame frames_[FEATURE_BUFFER_SIZE]{};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};
