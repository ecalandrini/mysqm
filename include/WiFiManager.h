#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "EEPROMLogger.h"
#include "config.h"

class WiFiManager {
public:
    WiFiManager();

    // Configures WiFi module interface settings
    bool begin();

    // Connects to WiFi, serializes packet to JSON, executes HTTP POST, and shuts down RF radio
    bool transmitPacket(const SQMDataPacket &packet);

    // Forces immediate radio shutdown and disconnects client (power saving)
    void sleep();

    // Active status helpers for Setup Mode diagnostic menus
    bool isConnected() const;
    String getIPAddress() const;
    int32_t getRSSI() const;
    String getSSID() const { return _ssid; }

private:
    String _ssid;
    String _password;
    String _postUrl;
    bool _isInitialized;

    // Helper to connect to the WiFi router blockingly with a timeout
    bool connectToRouter();
};

#endif // WIFI_MANAGER_H
