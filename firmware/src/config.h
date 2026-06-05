#pragma once

#ifndef MQTT_HOST
#define MQTT_HOST "192.168.1.100"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 18830
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASS"
#endif
#ifndef HOSTNAME
#define HOSTNAME "presence-hub"
#endif

#define MQTT_CLIENT_ID HOSTNAME
#define MQTT_TOPIC_RADAR "hub/radar"
#define MQTT_TOPIC_MODE "hub/mode"
#define MQTT_TOPIC_BUTTON "hub/button"
#define MQTT_TOPIC_DISPLAY "hub/display"
#define MQTT_TOPIC_GESTURE "hub/gesture"
#define MQTT_TOPIC_OTA "hub/ota/trigger"
#define MQTT_TOPIC_STATUS "hub/status"

#define HEARTBEAT_INTERVAL_MS 30000
#define RADAR_PUBLISH_INTERVAL_MS 1000
