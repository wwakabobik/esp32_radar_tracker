#include "display.h"
#include "pins.h"
#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);

bool Display::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (!u8g2.begin()) {
        Serial.println("Display init failed");
        ready_ = false;
        return false;
    }
    u8g2.setContrast(brightness_);
    ready_ = true;
    showBootMessage("Presence Hub", "starting...");
    return true;
}

void Display::setBrightness(uint8_t level) {
    brightness_ = level;
    if (ready_) u8g2.setContrast(level);
}

void Display::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled && ready_) {
        u8g2.clearDisplay();
        u8g2.sendBuffer();
    } else if (enabled && ready_) {
        drawSlots(lastSlots_);
    }
}

void Display::showBootMessage(const char *line1, const char *line2) {
    if (!ready_) return;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 14, line1);
    if (line2 && line2[0]) u8g2.drawStr(0, 30, line2);
    u8g2.sendBuffer();
}

void Display::drawSlots(const std::vector<DisplaySlot> &slots) {
    if (!ready_ || !enabled_) return;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    for (const auto &slot : slots) {
        const int y = 14 + static_cast<int>(slot.pos) * 16;
        if (y > 62) break;
        u8g2.drawStr(0, y, slot.text.c_str());
    }
    u8g2.sendBuffer();
}

void Display::render(const std::vector<DisplaySlot> &slots) {
    lastSlots_ = slots;
    drawSlots(slots);
}
