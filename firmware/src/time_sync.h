#pragma once

#include <Arduino.h>

namespace TimeSync {
bool ensure();
bool trySync();
bool ready();
double nowUnix();
String formatClockLocal();
}
