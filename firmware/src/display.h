#pragma once

#include <Arduino.h>
#include <vector>

struct DisplaySlot {
    uint8_t pos = 0;
    String text;
    String font = "medium";
    bool scroll = false;
};

class Display {
public:
    bool begin();
    void render(const std::vector<DisplaySlot> &slots, uint8_t brightness = 0);
    void showBootMessage(const char *line1, const char *line2 = "");
    void showOverlay(const char *line1, const char *line2 = "", uint16_t durationMs = 2000);
    void setBrightness(uint8_t level);
    void setSleepMode(const char *mode);
    void setEnabled(bool enabled);
    void setModeLetter(const char *mode);
    void loop();

private:
    static constexpr int kScrollMaxWidth = 106;

    bool ready_ = false;
    bool enabled_ = true;
    uint8_t brightness_ = 255;
    String sleepMode_ = "off";
    String modeLetter_;
    std::vector<DisplaySlot> lastSlots_;
    unsigned long overlayUntil_ = 0;
    String overlayLine1_;
    String overlayLine2_;
    bool scrollActive_ = false;
    int scrollOffset_ = 0;
    unsigned long scrollTickMs_ = 0;

    void drawSlots(const std::vector<DisplaySlot> &slots);
    void drawModeBadge();
    void drawLineText(int y, const DisplaySlot &slot);
    void applyFont(const String &font);
    int fontBaseline(const String &font) const;
    int fontStep(const String &font) const;
};
