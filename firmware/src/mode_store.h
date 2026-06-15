#pragma once

#include <Arduino.h>

namespace ModeStore {
void begin();
String load();
void save(const String &mode);
}
