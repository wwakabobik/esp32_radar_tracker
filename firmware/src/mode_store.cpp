#include "mode_store.h"

#include <Preferences.h>

static const char *PREFS_NS = "phub";
static const char *KEY_MODE = "mode";

void ModeStore::begin() {}

String ModeStore::load() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, true)) return "work";
    const String mode = prefs.getString(KEY_MODE, "work");
    prefs.end();
    if (mode == "sleep" || mode == "media") return mode;
    return "work";
}

void ModeStore::save(const String &mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NS, false)) return;
    prefs.putString(KEY_MODE, mode);
    prefs.end();
}
