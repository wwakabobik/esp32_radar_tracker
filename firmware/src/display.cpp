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
        if (ready_) {
            u8g2.clearDisplay();
            u8g2.sendBuffer();
        }
        return;
    }
    enabled_ = true;
    if (ready_ && !lastSlots_.empty()) drawSlots(lastSlots_);
}

void Display::applyFont(const String &font) {
    if (font == "large") {
        u8g2.setFont(u8g2_font_logisoso24_tf);
    } else if (font == "small") {
        u8g2.setFont(u8g2_font_6x12_tf);
    } else {
        u8g2.setFont(u8g2_font_helvB14_tf);
    }
}

int Display::fontBaseline(const String &font) const {
    if (font == "large") return 26;
    if (font == "small") return 11;
    return 15;
}

int Display::fontStep(const String &font) const {
    if (font == "large") return 28;
    if (font == "small") return 12;
    return 17;
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
    u8g2.setFont(u8g2_font_helvB18_tf);
    const int w = u8g2.getStrWidth(modeLetter_.c_str());
    u8g2.drawStr(128 - w, 17, modeLetter_.c_str());
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

void Display::drawLineText(int y, const DisplaySlot &slot) {
    applyFont(slot.font);
    const int textWidth = u8g2.getStrWidth(slot.text.c_str());
    if (!slot.scroll || textWidth <= kScrollMaxWidth) {
        u8g2.drawStr(0, y, slot.text.c_str());
        return;
    }
    String marquee = slot.text + "   ";
    const int marqueeWidth = u8g2.getStrWidth(marquee.c_str());
    const int offset = scrollOffset_ % marqueeWidth;
    const int clipTop = max(0, y - 16);
    u8g2.setClipWindow(0, clipTop, kScrollMaxWidth, min(63, y + 2));
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

    std::vector<const DisplaySlot *> visible;
    for (const auto &slot : slots) {
        if (!slot.text.isEmpty()) visible.push_back(&slot);
    }
    if (visible.empty()) {
        drawModeBadge();
        u8g2.sendBuffer();
        return;
    }

    const size_t lineCount = visible.size();
    scrollActive_ = false;
    for (const auto *slot : visible) {
        applyFont(slot->font);
        if (slot->scroll && u8g2.getStrWidth(slot->text.c_str()) > kScrollMaxWidth) {
            scrollActive_ = true;
        }
    }
    for (const auto *slot : visible) {
        applyFont(slot->font);
        int y = 38;
        if (lineCount == 1) {
            if (slot->font == "large") y = 42;
            else if (slot->font == "small") y = 34;
            else y = 38;
        } else if (slot->pos == 0) {
            if (slot->font == "large") y = 26;
            else if (slot->font == "small") y = 12;
            else y = 18;
        } else {
            if (slot->font == "large") y = 54;
            else if (slot->font == "small") y = 44;
            else y = 50;
        }
        if (y > 62) y = 62;
        drawLineText(y, *slot);
    }
    drawModeBadge();
    u8g2.sendBuffer();
}

void Display::render(const std::vector<DisplaySlot> &slots, uint8_t brightness) {
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
