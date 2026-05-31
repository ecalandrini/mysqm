#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <Arduino.h>
#include "EEPROMLogger.h"
#include "config.h"

// Define the LoRa Mode
enum LoRaMode {
    LORA_MODE_P2P,      // Direct Point-to-Point LoRa (using RadioLib or LoRa.h)
    LORA_MODE_WAN,      // LoRaWAN standard (using MCCI LMIC)
    LORA_MODE_MOCK      // Serial mock output for testing/debugging
};

class LoRaManager {
public:
    LoRaManager(LoRaMode mode = LORA_MODE_MOCK);

    // Initializes LoRa hardware
    bool begin();

    // Sends the packed 23-byte SQMDataPacket structure over the air
    bool transmitPacket(const SQMDataPacket &packet);

    // Enters deep sleep / standby mode for the LoRa transceiver
    void sleep();

    // Sets the LoRa mode (P2P vs WAN vs Mock)
    void setMode(LoRaMode mode) { _mode = mode; }

private:
    LoRaMode _mode;
    bool _isInitialized;

    // Helper functions for P2P and WAN transmission methods
    bool transmitP2P(const uint8_t *payload, size_t len);
    bool transmitWAN(const uint8_t *payload, size_t len);
    
    // Low level hardware sleep helpers
    void sleepP2P();
    void sleepWAN();
};

#endif // LORA_MANAGER_H
