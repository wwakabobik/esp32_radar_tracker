#include "tiny_ml.h"

#include "ai_config.h"

#include <math.h>

TinyMlEngine gTinyMl;

static float meanU8(const FeatureBuffer &buf, uint8_t (*getter)(const RadarReading &)) {
    if (buf.count() == 0) return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < buf.count(); ++i) {
        sum += static_cast<float>(getter(buf.at(i).reading));
    }
    return sum / static_cast<float>(buf.count());
}

static float varU8(const FeatureBuffer &buf, uint8_t (*getter)(const RadarReading &), float mean) {
    if (buf.count() < 2) return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < buf.count(); ++i) {
        const float d = static_cast<float>(getter(buf.at(i).reading)) - mean;
        sum += d * d;
    }
    return sum / static_cast<float>(buf.count() - 1);
}

static uint8_t peakGateMean(const FeatureBuffer &buf, bool moving) {
    if (buf.count() == 0) return 0;
    float sums[RADAR_GATE_COUNT]{};
    for (uint8_t i = 0; i < buf.count(); ++i) {
        const RadarReading &r = buf.at(i).reading;
        for (uint8_t g = 0; g < RADAR_GATE_COUNT; ++g) {
            sums[g] += moving ? r.moving_gates[g] : r.stationary_gates[g];
        }
    }
    uint8_t bestGate = 0;
    float best = 0.0f;
    for (uint8_t g = 0; g < RADAR_GATE_COUNT; ++g) {
        const float avg = sums[g] / static_cast<float>(buf.count());
        if (avg > best) {
            best = avg;
            bestGate = g;
        }
    }
    return bestGate;
}

static float gateSpectralCentroid(const FeatureBuffer &buf, bool moving) {
    float weighted = 0.0f;
    float total = 0.0f;
    for (uint8_t g = 0; g < RADAR_GATE_COUNT; ++g) {
        float energy = 0.0f;
        for (uint8_t i = 0; i < buf.count(); ++i) {
            const RadarReading &r = buf.at(i).reading;
            energy += moving ? r.moving_gates[g] : r.stationary_gates[g];
        }
        energy /= static_cast<float>(max<uint8_t>(1, buf.count()));
        weighted += static_cast<float>(g) * energy;
        total += energy;
    }
    return total > 0.0f ? (weighted / total) * 25.0f : 0.0f;
}

static float zeroCrossingRate(const FeatureBuffer &buf, uint8_t (*getter)(const RadarReading &)) {
    if (buf.count() < 3) return 0.0f;
    float mean = meanU8(buf, getter);
    uint16_t crossings = 0;
    bool above = getter(buf.at(0).reading) >= static_cast<uint8_t>(mean);
    for (uint8_t i = 1; i < buf.count(); ++i) {
        const bool nowAbove = getter(buf.at(i).reading) >= static_cast<uint8_t>(mean);
        if (nowAbove != above) ++crossings;
        above = nowAbove;
    }
    return static_cast<float>(crossings) / static_cast<float>(buf.count() - 1);
}

static float distVelocity(const FeatureBuffer &buf) {
    if (buf.count() < 2) return 0.0f;
    const FeatureFrame &first = buf.oldest();
    const FeatureFrame &last = buf.newest();
    const float dt = static_cast<float>(last.millis - first.millis);
    if (dt <= 0.0f) return 0.0f;
    const float dd = static_cast<float>(last.reading.dist) - static_cast<float>(first.reading.dist);
    return (dd / dt) * 1000.0f;  // cm/s
}

static float presentRatio(const FeatureBuffer &buf) {
    if (buf.count() == 0) return 0.0f;
    uint8_t hits = 0;
    for (uint8_t i = 0; i < buf.count(); ++i) {
        if (buf.at(i).reading.present) ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(buf.count());
}

static float movingRatio(const FeatureBuffer &buf) {
    if (buf.count() == 0) return 0.0f;
    uint8_t hits = 0;
    for (uint8_t i = 0; i < buf.count(); ++i) {
        if (buf.at(i).reading.moving) ++hits;
    }
    return static_cast<float>(hits) / static_cast<float>(buf.count());
}

static float crossCorrGates(const FeatureBuffer &buf) {
    if (buf.count() == 0) return 0.0f;
    float mSum = 0.0f;
    float sSum = 0.0f;
    float ms = 0.0f;
    for (uint8_t i = 0; i < buf.count(); ++i) {
        const RadarReading &r = buf.at(i).reading;
        mSum += r.m_energy;
        sSum += r.s_energy;
        ms += static_cast<float>(r.m_energy) * static_cast<float>(r.s_energy);
    }
    const float n = static_cast<float>(buf.count());
    const float denom = sqrtf(mSum * sSum);
    return denom > 0.0f ? (ms / denom) : 0.0f;
}

void TinyMlEngine::begin() {
    buffer_.clear();
    staticSinceMs_ = 0;
    lastWorkState_ = AiState::Vacant;
}

void TinyMlEngine::pushFrame(const RadarReading &reading, unsigned long nowMs) {
    buffer_.push(reading, nowMs);
}

void TinyMlEngine::setMode(const char *mode) {
    if (!mode) return;
    if (mode_ != mode) {
        mode_ = mode;
        buffer_.clear();
        staticSinceMs_ = 0;
    }
}

bool TinyMlEngine::extractFeatures(TinyMlFeatures &out) const {
    if (buffer_.count() < 4) return false;

    const float meanS = meanU8(buffer_, [](const RadarReading &r) { return r.s_energy; });
    const float meanM = meanU8(buffer_, [](const RadarReading &r) { return r.m_energy; });
    const float varS = varU8(buffer_, [](const RadarReading &r) { return r.s_energy; }, meanS);
    const float varM = varU8(buffer_, [](const RadarReading &r) { return r.m_energy; }, meanM);

    float meanDist = 0.0f;
    float maxS = 0.0f;
    float distVarAcc = 0.0f;
    for (uint8_t i = 0; i < buffer_.count(); ++i) {
        const RadarReading &r = buffer_.at(i).reading;
        meanDist += static_cast<float>(r.dist);
        maxS = max(maxS, static_cast<float>(r.s_energy));
    }
    meanDist /= static_cast<float>(buffer_.count());
    for (uint8_t i = 0; i < buffer_.count(); ++i) {
        const float d = static_cast<float>(buffer_.at(i).reading.dist) - meanDist;
        distVarAcc += d * d;
    }
    const float distStd = buffer_.count() > 1
                              ? sqrtf(distVarAcc / static_cast<float>(buffer_.count() - 1))
                              : 0.0f;

    out.values[0] = meanS;
    out.values[1] = varS;
    out.values[2] = meanM;
    out.values[3] = varM;
    out.values[4] = meanDist;
    out.values[5] = distVelocity(buffer_);
    out.values[6] = static_cast<float>(peakGateMean(buffer_, true));
    out.values[7] = static_cast<float>(peakGateMean(buffer_, false));
    out.values[8] = meanU8(buffer_, [](const RadarReading &r) { return r.m_gate_centroid; });
    out.values[9] = zeroCrossingRate(buffer_, [](const RadarReading &r) { return r.s_energy; }) * 100.0f;
    out.values[10] = crossCorrGates(buffer_) * 100.0f;
    out.values[11] = presentRatio(buffer_) * 100.0f;
    out.values[12] = movingRatio(buffer_) * 100.0f;
    out.values[13] = maxS;
    out.values[14] = distStd;
    out.values[15] = gateSpectralCentroid(buffer_, false);
    return true;
}

TinyMlResult TinyMlEngine::runMlp(const int8_t *weights, const int8_t *bias, uint8_t inputCount,
                                  uint8_t classCount, AiState baseState) const {
    TinyMlFeatures features{};
    TinyMlResult result{};
    if (!extractFeatures(features)) {
        result.state = baseState;
        result.confidence = 0;
        return result;
    }

    int32_t bestScore = INT32_MIN;
    uint8_t bestClass = 0;
    for (uint8_t c = 0; c < classCount; ++c) {
        int32_t score = bias[c];
        for (uint8_t i = 0; i < inputCount; ++i) {
            const int32_t x = static_cast<int32_t>(features.values[i]);
            score += static_cast<int32_t>(weights[c * inputCount + i]) * x / 32;
        }
        if (score > bestScore) {
            bestScore = score;
            bestClass = c;
        }
    }

    result.classId = bestClass;
    result.state = static_cast<AiState>(static_cast<uint8_t>(baseState) + bestClass);
    result.confidence = static_cast<uint8_t>(min<int32_t>(100, max<int32_t>(0, (bestScore + 40) * 2)));
    return result;
}

TinyMlResult TinyMlEngine::heuristicWork() const {
    TinyMlFeatures f{};
    TinyMlResult result{};
    if (!extractFeatures(f)) {
        result.state = AiState::Vacant;
        return result;
    }

    if (f.values[11] < 25.0f || f.values[13] < 8.0f) {
        result.state = AiState::Vacant;
        result.confidence = 80;
        return result;
    }
    if (f.values[3] > 120.0f && f.values[12] > 40.0f && f.values[1] < 20.0f) {
        result.state = AiState::EnvNoise;
        result.confidence = 70;
        return result;
    }
    if (f.values[1] < 8.0f && f.values[9] < 5.0f) {
        result.state = AiState::StaticFatigue;
        result.confidence = 75;
        return result;
    }
    result.state = AiState::ActiveFocus;
    result.confidence = 80;
    return result;
}

TinyMlResult TinyMlEngine::heuristicGesture() const {
    TinyMlFeatures f{};
    TinyMlResult result{AiState::GestureNone, 0, 0};
    if (!extractFeatures(f)) return result;

    if (f.values[5] > 25.0f && f.values[4] >= 12.0f && f.values[4] <= 35.0f) {
        result.state = AiState::GestureNext;
        result.confidence = 78;
        return result;
    }
    if (f.values[5] < -25.0f && f.values[4] >= 12.0f && f.values[4] <= 35.0f) {
        result.state = AiState::GesturePrev;
        result.confidence = 78;
        return result;
    }
    if (fabsf(f.values[5]) < 8.0f && f.values[14] < 4.0f && f.values[4] >= 15.0f && f.values[4] <= 35.0f) {
        result.state = AiState::GestureHover;
        result.confidence = 72;
        return result;
    }
    return result;
}

TinyMlResult TinyMlEngine::heuristicSleep() const {
    TinyMlFeatures f{};
    TinyMlResult result{AiState::SleepAbsent, 0, 0};
    if (!extractFeatures(f)) return result;

    if (f.values[11] < 20.0f || f.values[0] < 4.0f) {
        result.state = AiState::SleepAbsent;
        result.confidence = 80;
        return result;
    }
    if (f.values[3] > 80.0f || f.values[12] > 35.0f) {
        result.state = AiState::SleepRestless;
        result.confidence = 75;
        return result;
    }
    if (f.values[9] >= 8.0f && f.values[1] >= 4.0f) {
        result.state = AiState::SleepBreathingStable;
        result.confidence = 78;
        return result;
    }
    result.state = AiState::SleepRestless;
    result.confidence = 60;
    return result;
}

TinyMlResult TinyMlEngine::inferWork() {
    const AiConfig &cfg = gAiConfig.current();
    if (!cfg.enabled) {
        TinyMlResult r{};
        r.state = AiState::ActiveFocus;
        r.confidence = 0;
        return r;
    }

    TinyMlResult result = runMlp(WORK_WEIGHTS, WORK_BIAS, TINYML_FEATURE_COUNT,
                                 TINYML_WORK_CLASSES, AiState::Vacant);
    if (cfg.fallbackHeuristics && result.confidence < cfg.confidenceMin) {
        result = heuristicWork();
    }

    if (result.state == AiState::StaticFatigue) {
        if (staticSinceMs_ == 0) staticSinceMs_ = millis();
    } else {
        staticSinceMs_ = 0;
    }

    if (result.state == AiState::StaticFatigue && staticSinceMs_ > 0) {
        const unsigned long heldMin = (millis() - staticSinceMs_) / 60000UL;
        if (heldMin < cfg.fatigueMinutes) {
            result.state = AiState::ActiveFocus;
        }
    }

    lastWorkState_ = result.state;
    return result;
}

TinyMlResult TinyMlEngine::inferGesture() {
    const AiConfig &cfg = gAiConfig.current();
    if (!cfg.enabled) {
        return TinyMlResult{AiState::GestureNone, 0, 0};
    }

    TinyMlResult result = runMlp(GESTURE_WEIGHTS, GESTURE_BIAS, TINYML_FEATURE_COUNT,
                                 TINYML_GESTURE_CLASSES, AiState::GestureNone);
    if (cfg.fallbackHeuristics && result.confidence < cfg.confidenceMin) {
        result = heuristicGesture();
    }
    return result;
}

TinyMlResult TinyMlEngine::inferSleep() {
    const AiConfig &cfg = gAiConfig.current();
    if (!cfg.enabled) {
        return TinyMlResult{AiState::SleepAbsent, 0, 0};
    }

    TinyMlResult result = runMlp(SLEEP_WEIGHTS, SLEEP_BIAS, TINYML_FEATURE_COUNT,
                                 TINYML_SLEEP_CLASSES, AiState::SleepAbsent);
    if (cfg.fallbackHeuristics && result.confidence < cfg.confidenceMin) {
        result = heuristicSleep();
    }
    return result;
}

const char *TinyMlEngine::stateName(AiState state) const {
    return aiStateToString(state);
}
