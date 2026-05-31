#include "WiFiManager.h"

WiFiManager::WiFiManager() : 
    _ssid(WIFI_SSID), 
    _password(WIFI_PASSWORD), 
    _postUrl(WIFI_HTTP_POST_URL), 
    _isInitialized(false) {}

bool WiFiManager::begin() {
    // Configure WiFi STA mode but keep radio off until transmit is requested
    WiFi.persistent(false); // Disable flash wear from credential auto-saving
    WiFi.mode(WIFI_OFF);
    _isInitialized = true;
    return true;
}

bool WiFiManager::connectToRouter() {
    Serial.printf("[WiFi] Powering up radio and connecting to SSID: %s...\n", _ssid.c_str());
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _password.c_str());

    unsigned long start = millis();
    const unsigned long timeoutMs = WIFI_CONN_TIMEOUT_S * 1000UL;
    bool connected = false;

    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
        }
        delay(100);
    }

    if (connected) {
        Serial.printf("[WiFi] Router connection established. Local DHCP IP: %s, RSSI: %d dBm\n", 
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    } else {
        Serial.println("[WiFi] Connection TIMEOUT. AP router unreachable.");
        sleep(); // Turn off RF synthesizer to conserve power
        return false;
    }
}

bool WiFiManager::transmitPacket(const SQMDataPacket &packet) {
    if (!_isInitialized) return false;

    // 1. Establish connection to the local router
    if (!connectToRouter()) {
        return false;
    }

    // 2. Formulate JSON string (Avoids external library dependencies to optimize memory footprint)
    String jsonPayload = "{";
    jsonPayload += "\"timestamp\":" + String(packet.timestamp) + ",";
    jsonPayload += "\"sqm\":" + String(packet.sqm_reading, 4) + ",";
    jsonPayload += "\"temp\":" + String((float)packet.amb_temp / 100.0f, 2) + ",";
    jsonPayload += "\"press\":" + String(packet.amb_press) + ",";
    jsonPayload += "\"hum\":" + String((float)packet.amb_hum / 100.0f, 2) + ",";
    jsonPayload += "\"mag_x\":" + String(packet.mag_x) + ",";
    jsonPayload += "\"mag_y\":" + String(packet.mag_y) + ",";
    jsonPayload += "\"mag_z\":" + String(packet.mag_z) + ",";
    jsonPayload += "\"batt_mv\":" + String(packet.batt_mv) + ",";
    jsonPayload += "\"checksum\":" + String(packet.checksum);
    jsonPayload += "}";

    Serial.printf("[WiFi] Preparing to post JSON payload to: %s\n", _postUrl.c_str());
    Serial.printf("  Payload: %s\n", jsonPayload.c_str());

    // 3. Initiate HTTP Client POST transaction
    HTTPClient http;
    http.begin(_postUrl);
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.POST(jsonPayload);
    bool success = false;

    if (httpCode > 0) {
        Serial.printf("[WiFi] POST Transaction Completed. HTTP Status Code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
            success = true;
        }
    } else {
        Serial.printf("[WiFi] POST Transaction FAILED. Error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();

    // 4. CRUCIAL BATTERY SAVING STEP:
    // Disconnect connection and shut down the RF synthesizer.
    // Cuts active WiFi power draw (150-250mA) to 0uA during deep sleep.
    sleep();
    return success;
}

void WiFiManager::sleep() {
    WiFi.disconnect(true); // Disconnect STA client
    WiFi.mode(WIFI_OFF);   // Shut off WiFi synthesizers and RF blocks
    Serial.println("[WiFi] Radio powered off.");
}

bool WiFiManager::isConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::getIPAddress() const {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

int32_t WiFiManager::getRSSI() const {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return -100;
}
