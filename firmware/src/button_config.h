#pragma once

#include <Arduino.h>

struct ButtonHwConfig {
    uint8_t pin1 = 18;
    uint8_t pin2 = 5;
    bool activeLow = true;
    bool gpioProbe = false;
    bool buttonLearn = false;
};

class ButtonConfigStore {
public:
    void begin();
    const ButtonHwConfig &current() const { return cfg_; }
    void applyFromJson(const char *json);

private:
    void load(ButtonHwConfig &out);
    void save(const ButtonHwConfig &cfg);

    ButtonHwConfig cfg_;
};

extern ButtonConfigStore gButtonConfig;
