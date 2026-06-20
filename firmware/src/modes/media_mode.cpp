#include "modes/media_mode.h"

#include <ArduinoJson.h>

#include "ai_config.h"
#include "gesture_config.h"
#include "time_sync.h"

void MediaMode::onEnter(Radar &) {
    zoneEnterMs_ = 0;
    zoneArmed_ = true;
    lastNextMs_ = 0;
    lastPrevMs_ = 0;
    lastHoverMs_ = 0;
    lastDebugMs_ = 0;
}

void MediaMode::onExit(Radar &) {
    zoneEnterMs_ = 0;
    zoneArmed_ = true;
}

uint16_t MediaMode::gestureDist(const RadarReading &reading) const {
    return reading.dist > 0 ? reading.dist : radarNearestCm(reading);
}

bool MediaMode::inNearZone(uint16_t d) const {
    if (d == 0) return false;
    const GestureConfig &cfg = gGestureConfig.current();
    return d >= cfg.zoneMinCm && d <= cfg.zoneMaxCm;
}

void MediaMode::publishDebug(const RadarReading &reading, DebugCallback debugCb,
                             const TinyMlResult *ai) {
    if (!gGestureConfig.current().debug || !debugCb) return;
    const unsigned long now = millis();
    if (now - lastDebugMs_ < 200) return;
    lastDebugMs_ = now;

    const GestureConfig &cfg = gGestureConfig.current();
    const uint16_t d = gestureDist(reading);
    const bool inZone = inNearZone(d);

    StaticJsonDocument<256> doc;
    doc["dist"] = d;
    doc["presence"] = reading.present || d > 0;
    doc["in_zone"] = inZone;
    doc["zone_armed"] = zoneArmed_;
    if (ai) {
        doc["ai_gesture"] = aiStateToString(ai->state);
        doc["ai_confidence"] = ai->confidence;
    }
    if (TimeSync::ready()) doc["ts"] = TimeSync::nowUnix();
    if (inZone && zoneEnterMs_ > 0) {
        const unsigned long held = now - zoneEnterMs_;
        doc["hold_ms"] = held;
        doc["hold_left_ms"] = held < cfg.holdMs ? cfg.holdMs - held : 0;
    }

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    debugCb(buf);
}

void MediaMode::fallbackZoneHold(const RadarReading &reading, GestureCallback cb) {
    const GestureConfig &cfg = gGestureConfig.current();
    const unsigned long now = millis();
    const uint16_t d = gestureDist(reading);
    const bool inZone = inNearZone(d);

    if (!inZone) {
        zoneArmed_ = true;
        zoneEnterMs_ = 0;
        return;
    }

    if (!zoneArmed_) return;

    if (zoneEnterMs_ == 0) {
        zoneEnterMs_ = now;
        return;
    }

    if (now - zoneEnterMs_ < cfg.holdMs) return;

    if ((now - lastNextMs_) < cfg.debounceMs) {
        zoneArmed_ = false;
        zoneEnterMs_ = 0;
        return;
    }

    if (cb) {
        cb("next", static_cast<int>(d));
        lastNextMs_ = now;
    }
    zoneArmed_ = false;
    zoneEnterMs_ = 0;
}

void MediaMode::handleMlGesture(const TinyMlResult &ai, const RadarReading &reading,
                                GestureCallback cb) {
    const GestureConfig &cfg = gGestureConfig.current();
    const unsigned long now = millis();
    const uint16_t d = gestureDist(reading);

    if (ai.confidence < gAiConfig.current().confidenceMin) return;

    if (ai.state == AiState::GestureNext && (now - lastNextMs_) >= cfg.debounceMs) {
        if (cb) cb("next", static_cast<int>(d));
        lastNextMs_ = now;
        zoneArmed_ = false;
        zoneEnterMs_ = 0;
        return;
    }
    if (ai.state == AiState::GesturePrev && (now - lastPrevMs_) >= cfg.debounceMs) {
        if (cb) cb("prev", static_cast<int>(d));
        lastPrevMs_ = now;
        return;
    }
    if (ai.state == AiState::GestureHover && (now - lastHoverMs_) >= 400) {
        const int vol = static_cast<int>(min<uint16_t>(100, max<uint16_t>(0, (d - 10) * 4)));
        if (cb) cb("vol", vol);
        lastHoverMs_ = now;
    }
}

void MediaMode::onRadar(const RadarReading &reading, GestureCallback cb, DebugCallback debugCb,
                        const TinyMlResult *ai) {
    publishDebug(reading, debugCb, ai);

    if (ai && ai->confidence >= gAiConfig.current().confidenceMin &&
        ai->state != AiState::GestureNone) {
        handleMlGesture(*ai, reading, cb);
        return;
    }

    if (gAiConfig.current().fallbackHeuristics) {
        fallbackZoneHold(reading, cb);
    }
}
