#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <vector>

struct DisplaySlot {
    uint8_t pos = 0;
    String text;
};

class Display {
public:
    bool begin();
    void render(const std::vector<DisplaySlot> &slots);
    void showBootMessage(const char *line1, const char *line2 = "");
    void setBrightness(uint8_t level);
    void setEnabled(bool enabled);

private:
    bool ready_ = false;
    bool enabled_ = true;
    uint8_t brightness_ = 255;
    std::vector<DisplaySlot> lastSlots_;
    void drawSlots(const std::vector<DisplaySlot> &slots);
};
