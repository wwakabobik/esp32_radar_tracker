#include "buttons.h"

#include "button_config.h"
#include "pins.h"
#include <Arduino.h>

static uint8_t btnPins_[2] = {PIN_BTN_1, PIN_BTN_2};

static void setupButtonPin(uint8_t pin) {
    if (pin >= 34 && pin <= 39) {
        pinMode(pin, INPUT);
    } else {
        pinMode(pin, INPUT_PULLUP);
    }
}

void Buttons::applyConfig() {
    const auto &cfg = gButtonConfig.current();
    btnPins_[0] = cfg.pin1;
    btnPins_[1] = cfg.pin2;
    setupButtonPin(btnPins_[0]);
    setupButtonPin(btnPins_[1]);
    for (uint8_t i = 0; i < 2; ++i) {
        wasDown_[i] = false;
        longSent_[i] = false;
        pressStart_[i] = 0;
        lastChangeMs_[i] = 0;
        stableDown_[i] = false;
    }
    Serial.printf("Buttons on GPIO %u / %u (%s)\n", btnPins_[0], btnPins_[1],
                  cfg.activeLow ? "active LOW" : "active HIGH");
}

void Buttons::begin(Callback cb) {
    callback_ = cb;
    gButtonConfig.begin();
    applyConfig();
}

bool Buttons::readPressed(uint8_t pin) const {
    const bool level = digitalRead(pin);
    return gButtonConfig.current().activeLow ? !level : level;
}

void Buttons::loop() {
    const unsigned long now = millis();
    for (uint8_t i = 0; i < 2; ++i) {
        const bool down = readPressed(btnPins_[i]);
        if (down != stableDown_[i]) {
            if (now - lastChangeMs_[i] >= 30) {
                stableDown_[i] = down;
                lastChangeMs_[i] = now;
            }
        } else {
            lastChangeMs_[i] = now;
        }

        if (stableDown_[i] && !wasDown_[i]) {
            pressStart_[i] = now;
            longSent_[i] = false;
        }
        if (stableDown_[i] && wasDown_[i] && !longSent_[i] && (now - pressStart_[i] >= 800)) {
            longSent_[i] = true;
            if (callback_) callback_({static_cast<uint8_t>(i + 1), ButtonEvent::LongPress});
        }
        if (!stableDown_[i] && wasDown_[i]) {
            if (!longSent_[i] && callback_) {
                callback_({static_cast<uint8_t>(i + 1), ButtonEvent::Press});
            }
        }
        wasDown_[i] = stableDown_[i];
    }
}
