#pragma once

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 18830
#endif
#ifndef DISCOVERY_PORT
#define DISCOVERY_PORT 18832
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

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.7.1"
#endif

#define MQTT_CONNECT_TIMEOUT_SEC 1
#define MQTT_RETRY_MS 8000
#define MQTT_RETRY_MAX_MS 60000

#define MQTT_CLIENT_ID HOSTNAME
#define MQTT_TOPIC_RADAR "hub/radar"
#define MQTT_TOPIC_RADAR_RAW "hub/radar/raw"
#define MQTT_TOPIC_AI_STATE "hub/ai/state"
#define MQTT_TOPIC_MODE "hub/mode"
#define MQTT_TOPIC_BUTTON "hub/button"
#define MQTT_TOPIC_DISPLAY "hub/display"
#define MQTT_TOPIC_GESTURE "hub/gesture"
#define MQTT_TOPIC_OTA "hub/ota/trigger"
#define MQTT_TOPIC_STATUS "hub/status"
#define MQTT_TOPIC_SYNC_EVENTS "hub/sync/events"
#define MQTT_TOPIC_SYNC_ACK "hub/sync/ack"
#define MQTT_TOPIC_CONFIG "hub/config"
#define MQTT_TOPIC_DEBUG "hub/debug"

#define SYNC_BATCH_SIZE 25
#define SYNC_INTERVAL_MS 1000

#define HEARTBEAT_INTERVAL_MS 30000
#define RADAR_PUBLISH_INTERVAL_MS 1000
#define RADAR_PUBLISH_INTERVAL_MEDIA_MS 200
#define MEDIA_RADAR_POLLS 8
