#include "radar.h"

#include "pins.h"
#include "radar_config.h"

#include <MyLD2410.h>

static HardwareSerial RadarSerial(2);
static MyLD2410 sensor(RadarSerial);

static uint16_t strongestGateDistanceCm(
    byte gateWidthCm,
    uint8_t minEnergy,
    const MyLD2410::ValuesArray &moving,
    const MyLD2410::ValuesArray &stationary) {
    if (gateWidthCm == 0) gateWidthCm = 20;
    const byte maxGate = min(moving.N, stationary.N);
    byte bestGate = 255;
    uint8_t bestEnergy = 0;
    for (byte gate = 0; gate <= maxGate && gate <= 8; ++gate) {
        uint8_t energy = 0;
        if (gate <= moving.N) energy = max(energy, moving.values[gate]);
        if (gate <= stationary.N) energy = max(energy, stationary.values[gate]);
        if (energy >= minEnergy && energy > bestEnergy) {
            bestEnergy = energy;
            bestGate = gate;
        }
    }
    if (bestGate == 255) return 0;
    return static_cast<uint16_t>(bestGate) * gateWidthCm + gateWidthCm / 2;
}

static uint16_t pickPresenceDistance(
    uint16_t mDist,
    uint8_t mEnergy,
    uint16_t sDist,
    uint8_t sEnergy,
    uint16_t detected) {
    uint16_t best = 0;
    if (mDist > 0 && mEnergy >= 10) best = max(best, mDist);
    if (sDist > 0 && sEnergy >= 10) best = max(best, sDist);
    if (best > 0) return best;
    if (detected > 0) return detected;
    return 0;
}

static uint16_t pickGestureDistance(
    uint16_t gateDist,
    uint16_t mDist,
    uint8_t mEnergy,
    uint16_t sDist,
    uint8_t sEnergy,
    uint16_t detected,
    uint16_t farStationaryCapCm) {
    uint16_t fromSensor = UINT16_MAX;

    if (mDist > 0 && mEnergy >= 12) fromSensor = min(fromSensor, mDist);
    if (sDist > 0 && sEnergy >= 12 && sDist <= farStationaryCapCm) {
        fromSensor = min(fromSensor, sDist);
    }
    if (fromSensor != UINT16_MAX) return fromSensor;

    if (gateDist > 0) return gateDist;
    if (detected > 0 && detected <= farStationaryCapCm) return detected;
    return 0;
}

bool Radar::begin() {
    RadarSerial.begin(256000, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
    delay(200);
    if (!sensor.begin()) {
        Serial.println("Radar init failed");
        ready_ = false;
        return false;
    }
    Serial.println("Radar ready");
    ready_ = true;
    restoreWorkSensor();
    return true;
}

void Radar::applyGateLimits() {
    if (!ready_) return;
    const auto &cfg = gRadarConfig.current();
    const byte gate = highSensitivity_ ? cfg.gateSleep : cfg.gateWork;
    sensor.setMaxMovingGate(gate);
    sensor.setMaxStationaryGate(gate);
}

bool Radar::configureGestureSensor() {
    if (!ready_) return false;
    if (!sensor.configMode(true)) return false;
    const bool ok =
        sensor.enhancedMode(true) &&
        sensor.setResolution(true) &&  // 20 cm gates for close range
        sensor.setMaxGate(2, 2, 2);    // ignore targets beyond ~60 cm
    sensor.configMode(false);
    if (ok) Serial.println("Radar gesture profile ON (20cm, 3 gates)");
    return ok;
}

bool Radar::restoreWorkSensor() {
    if (!ready_) return false;
    if (!sensor.configMode(true)) return false;
    const bool ok = sensor.enhancedMode(true) && sensor.setResolution(true);
    applyGateLimits();
    sensor.configMode(false);
    if (ok) Serial.println("Radar work profile restored");
    return ok;
}

void Radar::setGestureProfile(bool enabled) {
    gestureProfile_ = enabled;
    if (!ready_) return;
    if (enabled) {
        configureGestureSensor();
    } else {
        restoreWorkSensor();
    }
}

void Radar::setHighSensitivity(bool enabled) {
    highSensitivity_ = enabled;
    if (!ready_ || gestureProfile_) return;
    applyGateLimits();
}

bool Radar::poll(RadarReading &out) {
    if (!ready_) {
        out = {};
        return false;
    }
    if (sensor.check() != MyLD2410::DATA) {
        return false;
    }

    const bool moving = sensor.movingTargetDetected();
    const bool still = sensor.stationaryTargetDetected();
    out.moving = moving;
    out.stationary = still;
    out.present = moving || still;
    out.s_dist = static_cast<uint16_t>(sensor.stationaryTargetDistance());
    out.m_dist = static_cast<uint16_t>(sensor.movingTargetDistance());
    out.s_energy = sensor.stationaryTargetSignal();
    out.m_energy = sensor.movingTargetSignal();
    const uint16_t detected = static_cast<uint16_t>(sensor.detectedDistance());

    out.gate_dist = 0;
    byte gateWidth = sensor.getResolution();
    if (gateWidth == 0) gateWidth = gestureProfile_ ? 20 : 75;
    if (sensor.inEnhancedMode()) {
        out.gate_dist = strongestGateDistanceCm(
            gateWidth, gestureProfile_ ? 18 : 22,
            sensor.getMovingSignals(), sensor.getStationarySignals());
    }

    if (gestureProfile_) {
        out.dist = pickGestureDistance(
            out.gate_dist, out.m_dist, out.m_energy,
            out.s_dist, out.s_energy, detected, 45);
    } else {
        out.dist = pickPresenceDistance(
            out.m_dist, out.m_energy, out.s_dist, out.s_energy, detected);
    }
    out.presence_dist = pickPresenceDistance(
        out.m_dist, out.m_energy, out.s_dist, out.s_energy, detected);

    if (out.dist > 0) out.present = true;

    const auto &signals = sensor.getMovingSignals();
    float weighted = 0.0f;
    float total = 0.0f;
    for (byte i = 0; i <= signals.N; ++i) {
        const byte energy = signals.values[i];
        if (energy > 0) {
            weighted += static_cast<float>(i) * static_cast<float>(energy);
            total += static_cast<float>(energy);
        }
    }
    out.m_gate_centroid = total > 0.0f
                              ? static_cast<uint8_t>(min(80.0f, (weighted / total) * 10.0f))
                              : 0;

    memset(out.moving_gates, 0, sizeof(out.moving_gates));
    memset(out.stationary_gates, 0, sizeof(out.stationary_gates));
    if (sensor.inEnhancedMode()) {
        const auto &movingGates = sensor.getMovingSignals();
        const auto &stillGates = sensor.getStationarySignals();
        const byte maxGate = min(min(movingGates.N, stillGates.N), static_cast<byte>(RADAR_GATE_COUNT - 1));
        for (byte gate = 0; gate <= maxGate; ++gate) {
            if (gate <= movingGates.N) out.moving_gates[gate] = movingGates.values[gate];
            if (gate <= stillGates.N) out.stationary_gates[gate] = stillGates.values[gate];
        }
    }
    return true;
}
