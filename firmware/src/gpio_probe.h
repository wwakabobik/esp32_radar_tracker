#pragma once

#include <Arduino.h>

class MqttHub;

class GpioProbe {
public:
    void begin();
    void loop(MqttHub &mqtt);

private:
    static const uint8_t kCandidatePins[];
    static const size_t kPinCount;

    uint8_t lastState_[18] = {0};
    unsigned long lastHeartbeatMs_ = 0;
    bool initialized_ = false;

    uint8_t readPin(uint8_t pin) const;
};
