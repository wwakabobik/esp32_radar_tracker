#pragma once

#include <Arduino.h>
#include <WiFi.h>

struct HubEndpoint {
    IPAddress mqttHost;
    uint16_t mqttPort = 18830;
    IPAddress otaHost;
    uint16_t otaPort = 18081;
    bool valid = false;
};

class HubDiscovery {
public:
    bool loadCached(HubEndpoint &out);
    void saveCached(const HubEndpoint &endpoint);
    void clearCached();
    bool discover(HubEndpoint &out);
    bool pollAnnouncement(HubEndpoint &out);
};
