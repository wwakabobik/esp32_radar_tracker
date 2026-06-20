#include "display.h"

#include "pins.h"

#include <U8g2lib.h>
#include <Wire.h>

namespace {

bool isDurationPart(const String &part) {
    const int colon = part.indexOf(':');
    if (colon <= 0 || colon >= (int)part.length() - 1) return false;
    for (unsigned i = 0; i < part.length(); ++i) {
        if (i == (unsigned)colon) continue;
        if (!isDigit(part[i])) return false;
    }
    return true;
}

bool isDurationSuffix(const String &tail) {
    const int slash = tail.indexOf('/');
    if (slash <= 0 || slash >= (int)tail.length() - 1) return false;
    return isDurationPart(tail.substring(0, slash)) && isDurationPart(tail.substring(slash + 1));
}

String scrollCompareKey(const DisplaySlot &slot) {
    if (!slot.scroll || slot.text.isEmpty()) return slot.text;
    const int sep = slot.text.lastIndexOf("  ");
    if (sep < 0) return slot.text;
    const String tail = slot.text.substring(sep + 2);
    if (isDurationSuffix(tail)) return slot.text.substring(0, sep);
    return slot.text;
}

String scrollSignature(const std::vector<DisplaySlot> &slots) {
    String sig;
    for (const auto &slot : slots) {
        if (!slot.scroll) continue;
        sig += String(slot.pos);
        sig += ':';
        sig += slot.font;
        sig += ':';
        sig += scrollCompareKey(slot);
        sig += '|';
    }
    return sig;
}

}  // namespace

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

void Display::clearScreen() {
    if (!ready_) return;
    u8g2.clearDisplay();
    u8g2.clearBuffer();
    u8g2.sendBuffer();
}

void Display::setBrightness(uint8_t level) {
    brightness_ = level;
    if (ready_) u8g2.setContrast(level);
}

void Display::setSleepMode(const char *mode) {
    sleepMode_ = mode ? mode : "off";
}

void Display::setEnabled(bool enabled) {
    if (sleepMode_ == "off" && !enabled) {
        enabled_ = false;
        clearScreen();
        return;
    }
    enabled_ = true;
    if (ready_ && !lastSlots_.empty()) drawSlots(lastSlots_);
}

void Display::applyFont(const String &font) {
    if (font == "xlarge") {
        u8g2.setFont(u8g2_font_logisoso32_tf);
    } else if (font == "large") {
        u8g2.setFont(u8g2_font_logisoso24_tf);
    } else if (font == "small") {
        u8g2.setFont(u8g2_font_6x12_tf);
    } else {
        u8g2.setFont(u8g2_font_helvB14_tf);
    }
}

String Display::effectiveFont(const String &font) const {
    if (configuredLineCount_ != 1) return font;
    if (font == "small") return "medium";
    if (font == "medium") return "large";
    if (font == "large") return "xlarge";
    return font;
}

int Display::lineBaseline(const String &font, bool singleLine, uint8_t pos) const {
    if (singleLine) {
        if (font == "xlarge") return 52;
        if (font == "large") return 46;
        if (font == "small") return 38;
        return 42;
    }
    if (pos == 0) {
        if (font == "xlarge") return 30;
        if (font == "large") return 26;
        if (font == "small") return 12;
        return 18;
    }
    if (font == "xlarge") return 58;
    if (font == "large") return 54;
    if (font == "small") return 44;
    return 50;
}

int Display::scrollMaxWidth() const {
    return configuredLineCount_ == 1 ? kScrollMaxWidthSingle : kScrollMaxWidth;
}

void Display::showBootMessage(const char *line1, const char *line2) {
    if (!ready_) return;
    u8g2.clearBuffer();
    applyFont("medium");
    u8g2.drawStr(0, 15, line1);
    if (line2 && line2[0]) u8g2.drawStr(0, 32, line2);
    u8g2.sendBuffer();
}

void Display::setModeLetter(const char *mode) {
    String next;
    if (mode && mode[0]) {
        if (strcmp(mode, "work") == 0) next = "W";
        else if (strcmp(mode, "sleep") == 0) next = "S";
        else if (strcmp(mode, "media") == 0) next = "M";
    }
    if (next == modeLetter_) return;
    modeLetter_ = next;
    if (!ready_) return;
    if (overlayUntil_ && millis() < overlayUntil_) return;
    if (enabled_ && !lastSlots_.empty()) {
        drawSlots(lastSlots_);
        return;
    }
    if (modeLetter_.isEmpty()) return;
    u8g2.setContrast(enabled_ ? brightness_ : min<uint8_t>(brightness_, 48));
    u8g2.clearBuffer();
    drawModeBadge();
    u8g2.sendBuffer();
}

void Display::drawModeBadge() {
    if (modeLetter_.isEmpty()) return;
    u8g2.setFont(u8g2_font_helvR08_tr);
    const int w = u8g2.getStrWidth(modeLetter_.c_str());
    u8g2.drawStr(128 - w - 1, 10, modeLetter_.c_str());
}

void Display::showOverlay(const char *line1, const char *line2, uint16_t durationMs) {
    if (!ready_) return;
    overlayLine1_ = line1 ? line1 : "";
    overlayLine2_ = line2 ? line2 : "";
    overlayUntil_ = millis() + durationMs;
    u8g2.clearBuffer();
    applyFont("medium");
    u8g2.drawStr(0, 20, overlayLine1_.c_str());
    if (overlayLine2_.length()) u8g2.drawStr(0, 40, overlayLine2_.c_str());
    drawModeBadge();
    u8g2.sendBuffer();
}

void Display::loop() {
    if (overlayUntil_ && millis() >= overlayUntil_) {
        overlayUntil_ = 0;
        if (!lastSlots_.empty()) drawSlots(lastSlots_);
    }
    if (scrollActive_ && (!overlayUntil_ || millis() >= overlayUntil_)) {
        const unsigned long now = millis();
        if (now - scrollTickMs_ >= 180) {
            scrollTickMs_ = now;
            scrollOffset_ += 2;
            drawSlots(lastSlots_);
        }
    }
}

int Display::centeredBaseline(const String &font) const {
    if (font == "xlarge") return 44;
    if (font == "large") return 40;
    if (font == "small") return 36;
    return 38;
}

void Display::drawLineText(int y, const DisplaySlot &slot, const String &font, int scrollWidth) {
    applyFont(font);
    const int textWidth = u8g2.getStrWidth(slot.text.c_str());
    int x = 0;
    if (slot.center && !slot.scroll) {
        x = max(0, (128 - textWidth) / 2);
    }
    if (!slot.scroll || textWidth <= scrollWidth) {
        u8g2.drawStr(x, y, slot.text.c_str());
        return;
    }
    String marquee = slot.text + "   ";
    const int marqueeWidth = u8g2.getStrWidth(marquee.c_str());
    const int offset = scrollOffset_ % marqueeWidth;
    const int clipTop = max(0, y - 20);
    u8g2.setClipWindow(0, clipTop, scrollWidth, min(63, y + 4));
    u8g2.drawStr(-offset, y, marquee.c_str());
    u8g2.setMaxClipWindow();
}

void Display::drawSlots(const std::vector<DisplaySlot> &slots) {
    if (!ready_ || !enabled_) return;
    if (overlayUntil_ && millis() < overlayUntil_) return;

    uint8_t contrast = brightness_;
    if (sleepMode_ == "minimal") contrast = min<uint8_t>(brightness_, 40);
    u8g2.setContrast(contrast);

    u8g2.clearBuffer();

    if (configuredLineCount_ == 0) {
        u8g2.sendBuffer();
        return;
    }

    std::vector<const DisplaySlot *> visible;
    for (const auto &slot : slots) {
        if (slot.text.isEmpty()) continue;
        if (slot.pos >= configuredLineCount_) continue;
        visible.push_back(&slot);
    }
    if (visible.empty()) {
        if (configuredLineCount_ != 1) drawModeBadge();
        u8g2.sendBuffer();
        return;
    }

    const bool singleLine = configuredLineCount_ == 1;
    const int scrollWidth = scrollMaxWidth();
    scrollActive_ = false;
    for (const auto *slot : visible) {
        const String font = effectiveFont(slot->font);
        applyFont(font);
        if (slot->scroll && u8g2.getStrWidth(slot->text.c_str()) > scrollWidth) {
            scrollActive_ = true;
        }
    }
    for (const auto *slot : visible) {
        const String font = effectiveFont(slot->font);
        const bool loneCentered = slot->center && visible.size() == 1;
        int y = loneCentered ? centeredBaseline(font) : lineBaseline(font, singleLine, slot->pos);
        if (y > 62) y = 62;
        drawLineText(y, *slot, font, scrollWidth);
    }
    if (!singleLine) drawModeBadge();
    u8g2.sendBuffer();
}

void Display::render(const std::vector<DisplaySlot> &slots, uint8_t brightness, uint8_t lineCount) {
    const uint8_t nextLineCount = min<uint8_t>(lineCount, 2);
    const bool layoutChanged =
        nextLineCount != configuredLineCount_ || slots.size() != lastSlots_.size();
    if (layoutChanged && ready_) {
        clearScreen();
        scrollOffset_ = 0;
        scrollActive_ = false;
    }

    configuredLineCount_ = nextLineCount;

    const String nextScrollSig = scrollSignature(slots);
    const String prevScrollSig = scrollSignature(lastSlots_);
    if (nextScrollSig != prevScrollSig) {
        scrollOffset_ = 0;
        scrollTickMs_ = millis();
    }
    lastSlots_ = slots;
    if (brightness > 0) setBrightness(brightness);
    drawSlots(slots);
}
