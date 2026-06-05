#pragma once

#include <HTTPUpdate.h>
#include <WiFiClient.h>

namespace Ota {
bool performUpdate(const String &url);
}
