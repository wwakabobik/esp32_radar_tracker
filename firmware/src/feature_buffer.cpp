#include "feature_buffer.h"

void FeatureBuffer::push(const RadarReading &reading, unsigned long nowMs) {
    frames_[head_].reading = reading;
    frames_[head_].millis = nowMs;
    head_ = static_cast<uint8_t>((head_ + 1) % FEATURE_BUFFER_SIZE);
    if (count_ < FEATURE_BUFFER_SIZE) {
        ++count_;
    }
}

const FeatureFrame &FeatureBuffer::at(uint8_t index) const {
    if (index >= count_) {
        return frames_[0];
    }
    const uint8_t start = count_ < FEATURE_BUFFER_SIZE
                              ? 0
                              : head_;
    const uint8_t pos = static_cast<uint8_t>((start + index) % FEATURE_BUFFER_SIZE);
    return frames_[pos];
}

const FeatureFrame &FeatureBuffer::newest() const {
    if (count_ == 0) {
        return frames_[0];
    }
    const uint8_t pos = head_ == 0 ? FEATURE_BUFFER_SIZE - 1 : static_cast<uint8_t>(head_ - 1);
    return frames_[pos];
}

const FeatureFrame &FeatureBuffer::oldest() const {
    return at(0);
}

void FeatureBuffer::clear() {
    head_ = 0;
    count_ = 0;
}
