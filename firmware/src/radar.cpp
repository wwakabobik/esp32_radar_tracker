#include "radar.h"
#include "pins.h"
#include <MyLD2410.h>

static HardwareSerial RadarSerial(2);
static MyLD2410 sensor(RadarSerial);

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
    return true;
}

void Radar::setHighSensitivity(bool enabled) {
    highSensitivity_ = enabled;
    if (!ready_) return;
    if (enabled) {
        sensor.setMaxDistanceGate(8);
        sensor.setMaxMovingDistanceGate(8);
        sensor.setMaxStationaryDistanceGate(8);
    } else {
        sensor.setMaxDistanceGate(6);
        sensor.setMaxMovingDistanceGate(6);
        sensor.setMaxStationaryDistanceGate(6);
    }
}

bool Radar::poll(RadarReading &out) {
    if (!ready_) {
        out = {};
        return false;
    }
    if (!sensor.check()) {
        return false;
    }

    const bool moving = sensor.presenceDetected();
    const bool still = sensor.stationaryTargetDetected();
    out.present = moving || still;
    out.dist = sensor.detectionDistance();
    out.s_dist = sensor.stationaryTargetDistance();
    out.m_dist = sensor.movingTargetDistance();
    out.s_energy = sensor.stationaryTargetSignal();
    out.m_energy = sensor.movingTargetSignal();
    return true;
}
