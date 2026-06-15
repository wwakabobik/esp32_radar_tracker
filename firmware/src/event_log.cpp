#include "event_log.h"

#include <LittleFS.h>
#include <Preferences.h>

#include "time_sync.h"

EventLog gEventLog;

bool EventLog::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return false;
    }
    if (!loadMeta()) return false;
    Serial.printf("EventLog ready, next_id=%u\n", nextId_);
    return true;
}

bool EventLog::loadMeta() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) return false;
    nextId_ = prefs.getUInt(KEY_NEXT_ID, 1);
    if (nextId_ == 0) nextId_ = 1;
    prefs.end();
    return true;
}

bool EventLog::saveMeta() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) return false;
    prefs.putUInt(KEY_NEXT_ID, nextId_);
    prefs.end();
    return true;
}

uint32_t EventLog::append(const char *type, const char *mode, JsonObjectConst data) {
    StaticJsonDocument<128> empty;
    return append(type, mode, empty.as<JsonObject>());
}

uint32_t EventLog::append(const char *type, const char *mode, JsonObject data) {
    const uint32_t id = nextId_++;
    const double ts = TimeSync::nowUnix();

    String dataJson;
    if (!data.isNull()) {
        serializeJson(data, dataJson);
    } else {
        dataJson = "{}";
    }

    if (!writeLine(id, ts, type, mode, dataJson)) {
        nextId_--;
        return 0;
    }
    saveMeta();
    return id;
}

bool EventLog::writeLine(uint32_t id, double ts, const char *type, const char *mode, const String &dataJson) {
    File file = LittleFS.open(EVENTS_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("EventLog append failed");
        return false;
    }

    StaticJsonDocument<384> doc;
    doc["id"] = id;
    if (ts > 0) doc["ts"] = ts;
    doc["type"] = type;
    doc["mode"] = mode;

    StaticJsonDocument<128> dataDoc;
    if (deserializeJson(dataDoc, dataJson) == DeserializationError::Ok) {
        doc["data"] = dataDoc;
    } else {
        doc["data"] = JsonObject();
    }

    serializeJson(doc, file);
    file.print('\n');
    file.close();
    return true;
}

bool EventLog::readBatch(uint32_t afterId, uint32_t maxCount, JsonArray out, uint32_t &lastReadId) const {
    File file = LittleFS.open(EVENTS_PATH, FILE_READ);
    if (!file) return false;

    lastReadId = afterId;
    uint32_t added = 0;
    while (file.available() && added < maxCount) {
        const String line = file.readStringUntil('\n');
        if (line.isEmpty()) continue;

        StaticJsonDocument<384> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        const uint32_t id = doc["id"] | 0;
        if (id <= afterId) continue;

        out.add(doc);
        lastReadId = id;
        added++;
    }
    file.close();
    return added > 0;
}

size_t EventLog::count() const {
    File file = LittleFS.open(EVENTS_PATH, FILE_READ);
    if (!file) return 0;
    size_t n = 0;
    while (file.available()) {
        if (file.readStringUntil('\n').length() > 0) n++;
    }
    file.close();
    return n;
}

uint32_t EventLog::pendingAfter(uint32_t ackId) const {
    File file = LittleFS.open(EVENTS_PATH, FILE_READ);
    if (!file) return 0;
    uint32_t pending = 0;
    while (file.available()) {
        const String line = file.readStringUntil('\n');
        if (line.isEmpty()) continue;
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
        if ((doc["id"] | 0) > ackId) pending++;
    }
    file.close();
    return pending;
}

bool EventLog::ackThrough(uint32_t ackId) {
    File in = LittleFS.open(EVENTS_PATH, FILE_READ);
    if (!in) return true;

    File out = LittleFS.open("/events.tmp", FILE_WRITE);
    if (!out) {
        in.close();
        return false;
    }

    while (in.available()) {
        const String line = in.readStringUntil('\n');
        if (line.isEmpty()) continue;
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) {
            out.println(line);
            continue;
        }
        const uint32_t id = doc["id"] | 0;
        if (id > ackId) out.println(line);
    }
    in.close();
    out.close();

    LittleFS.remove(EVENTS_PATH);
    LittleFS.rename("/events.tmp", EVENTS_PATH);
    Serial.printf("EventLog ack through %u, remaining=%u\n", ackId, static_cast<unsigned>(count()));
    return true;
}
