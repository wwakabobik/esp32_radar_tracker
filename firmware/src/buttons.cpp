#include "buttons.h"
#include "pins.h"

static const uint8_t BTN_PINS[2] = {PIN_BTN_1, PIN_BTN_2};

void Buttons::begin(Callback cb) {
    callback_ = cb;
    pinMode(PIN_BTN_1, INPUT_PULLUP);
    pinMode(PIN_BTN_2, INPUT_PULLUP);
}

bool Buttons::readPressed(uint8_t pin) const {
    return digitalRead(pin) == LOW;
}

void Buttons::loop() {
    const unsigned long now = millis();
    for (uint8_t i = 0; i < 2; ++i) {
        const bool down = readPressed(BTN_PINS[i]);
        if (down && !wasDown_[i]) {
            pressStart_[i] = now;
            longSent_[i] = false;
        }
        if (down && wasDown_[i] && !longSent_[i] && (now - pressStart_[i] >= 800)) {
            longSent_[i] = true;
            if (callback_) callback_({static_cast<uint8_t>(i + 1), ButtonEvent::LongPress});
        }
        if (!down && wasDown_[i]) {
            if (!longSent_[i] && callback_) {
                callback_({static_cast<uint8_t>(i + 1), ButtonEvent::Press});
            }
        }
        wasDown_[i] = down;
    }
}
