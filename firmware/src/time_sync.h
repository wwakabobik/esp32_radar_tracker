#pragma once

#include <Arduino.h>

namespace TimeSync {
bool ensure();
bool ready();
double nowUnix();
}
