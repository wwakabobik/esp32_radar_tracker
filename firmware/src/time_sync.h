#pragma once

#include <Arduino.h>

namespace TimeSync {
void begin();
bool ensure();
bool trySync();
bool ready();
double nowUnix();
int32_t utcOffsetSec();
void setUtcOffsetSec(int32_t sec);
String formatClockLocal();
}
