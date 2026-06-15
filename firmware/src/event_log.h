#pragma once

#include <ArduinoJson.h>

class EventLog {
public:
    bool begin();
    uint32_t append(const char *type, const char *mode, JsonObjectConst data = JsonObjectConst());
    uint32_t append(const char *type, const char *mode, JsonObject data);
    bool readBatch(uint32_t afterId, uint32_t maxCount, JsonArray out, uint32_t &lastReadId) const;
    bool ackThrough(uint32_t ackId);
    uint32_t pendingAfter(uint32_t ackId) const;
    size_t count() const;

private:
    static constexpr const char *EVENTS_PATH = "/events.log";
    static constexpr const char *PREFS_NS = "evlog";
    static constexpr const char *KEY_NEXT_ID = "next_id";

    uint32_t nextId_ = 1;
    bool writeLine(uint32_t id, double ts, const char *type, const char *mode, const String &dataJson);
    bool loadMeta();
    bool saveMeta();
};

extern EventLog gEventLog;
