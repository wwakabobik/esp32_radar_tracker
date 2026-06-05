#pragma once

#include <functional>

enum class ButtonEvent { Press, LongPress };

struct ButtonMessage {
    uint8_t id;
    ButtonEvent event;
};

class Buttons {
public:
    using Callback = std::function<void(const ButtonMessage &)>;
    void begin(Callback cb);
    void loop();

private:
    Callback callback_;
    unsigned long pressStart_[2] = {0, 0};
    bool wasDown_[2] = {false, false};
    bool longSent_[2] = {false, false};
    bool readPressed(uint8_t pin) const;
};
