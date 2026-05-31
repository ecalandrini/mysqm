#include "LoRaManager.h"

// Set these to 1 if you have installed the respective libraries and wish to enable physical transceivers:
#define ENABLE_PHYSICAL_LORA_P2P 0 // Requires <LoRa.h> library (e.g. sandeepmistry/LoRa)
#define ENABLE_PHYSICAL_LORAWAN  0 // Requires <lmic.h> library (e.g. mcci-catena/MCCI LoRaWAN LMIC)

#if ENABLE_PHYSICAL_LORA_P2P
#include <SPI.h>
#include <LoRa.h>
// Pinout definitions for standard SPI SX1276 LoRa transceiver
#define LORA_NSS_PIN    18
#define LORA_RST_PIN    14
#define LORA_DIO0_PIN   26
#endif

#if ENABLE_PHYSICAL_LORAWAN
#include <lmic.h>
#include <hal/hal.h>
// Pin mapping for MCCI LMIC library
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32}, // DIO0, DIO1, DIO2
};
#endif

LoRaManager::LoRaManager(LoRaMode mode) : 
    _mode(mode), 
    _isInitialized(false) {}

bool LoRaManager::begin() {
    switch (_mode) {
        case LORA_MODE_MOCK:
            Serial.println("[LoRa] Initializing in MOCK mode (No hardware transceiver required).");
            _isInitialized = true;
            return true;

        case LORA_MODE_P2P:
#if ENABLE_PHYSICAL_LORA_P2P
            Serial.println("[LoRa] Initializing hardware transceiver in P2P mode (433/868/915 MHz)...");
            LoRa.setPins(LORA_NSS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
            // Initialize LoRa at 868.1 MHz or 915.0 MHz based on local regulation
            if (!LoRa.begin(868E6)) { 
                Serial.println("[LoRa] SX1276 initialization failed.");
                return false;
            }
            // Put to standby to conserve power
            LoRa.idle();
            _isInitialized = true;
            return true;
#else
            Serial.println("[LoRa] ERROR: Physical P2P compiled out. Set ENABLE_PHYSICAL_LORA_P2P to 1.");
            return false;
#endif

        case LORA_MODE_WAN:
#if ENABLE_PHYSICAL_LORAWAN
            Serial.println("[LoRa] Initializing LMIC stack for LoRaWAN...");
            os_init();
            LMIC_reset();
            // LoRaWAN setup requires keys (OTAA or ABP), which are configured in LMIC config
            _isInitialized = true;
            return true;
#else
            Serial.println("[LoRa] ERROR: Physical LoRaWAN compiled out. Set ENABLE_PHYSICAL_LORAWAN to 1.");
            return false;
#endif

        default:
            return false;
    }
}

bool LoRaManager::transmitPacket(const SQMDataPacket &packet) {
    if (!_isInitialized) {
        Serial.println("[LoRa] Transmit failed: Manager not initialized.");
        return false;
    }

    const uint8_t *payload = (const uint8_t *)&packet;
    size_t len = sizeof(SQMDataPacket);

    // Output formatted serial hex representation of packet (for gateway validation tests)
    Serial.printf("[LoRa] Preparing to transmit binary payload (%d bytes):\n  HEX: ", len);
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X ", payload[i]);
    }
    Serial.println();

    // Verify packet checksum before transmission
    uint8_t computedCrc = EEPROMLogger::calculateCRC8(payload, len - 1);
    if (packet.checksum != computedCrc) {
        Serial.println("[LoRa] WARNING: Packet checksum mismatch! Corrupted memory.");
        return false;
    }
    Serial.printf("  Checksum verified: 0x%02X (OK)\n", packet.checksum);

    // Perform the mode-specific transmission
    switch (_mode) {
        case LORA_MODE_MOCK:
            Serial.println("[LoRa] [MOCK] Packet transmitted successfully (Simulated).");
            return true;

        case LORA_MODE_P2P:
            return transmitP2P(payload, len);

        case LORA_MODE_WAN:
            return transmitWAN(payload, len);

        default:
            return false;
    }
}

bool LoRaManager::transmitP2P(const uint8_t *payload, size_t len) {
#if ENABLE_PHYSICAL_LORA_P2P
    LoRa.beginPacket();
    LoRa.write(payload, len);
    int status = LoRa.endPacket(); // Blocking transmit completion
    if (status == 1) {
        Serial.println("[LoRa] [P2P] Packet transmitted successfully.");
        return true;
    } else {
        Serial.println("[LoRa] [P2P] Packet transmission failed.");
        return false;
    }
#else
    (void)payload; (void)len;
    return false;
#endif
}

bool LoRaManager::transmitWAN(const uint8_t *payload, size_t len) {
#if ENABLE_PHYSICAL_LORAWAN
    // LoRaWAN transmission is queue-based in LMIC
    // Enqueue packet at port 1, unconfirmed
    lmic_tx_action_t action = LMIC_setTxData2(1, (xref2u1_t)payload, len, 0);
    if (action == 0) {
        Serial.println("[LoRa] [LoRaWAN] Packet queued for transmission.");
        // Under deep sleep architectures, LMIC would need to be run synchronously 
        // to completion before shutting down the stack.
        // We run the LMIC loop until TX complete flag is set.
        unsigned long start = millis();
        while ((LMIC.opmode & OP_TXDATA) || (LMIC.opmode & OP_TXRXPEND)) {
            os_runloop_once();
            delay(1);
            if (millis() - start > 10000) { // 10s timeout
                Serial.println("[LoRa] [LoRaWAN] TX Timeout.");
                return false;
            }
        }
        return true;
    } else {
        Serial.printf("[LoRa] [LoRaWAN] Queue failed with status %d\n", action);
        return false;
    }
#else
    (void)payload; (void)len;
    return false;
#endif
}

void LoRaManager::sleep() {
    if (!_isInitialized) return;

    switch (_mode) {
        case LORA_MODE_MOCK:
            Serial.println("[LoRa] [MOCK] Transceiver entering standby mode.");
            break;

        case LORA_MODE_P2P:
            sleepP2P();
            break;

        case LORA_MODE_WAN:
            sleepWAN();
            break;
    }
}

void LoRaManager::sleepP2P() {
#if ENABLE_PHYSICAL_LORA_P2P
    LoRa.sleep(); // SX1276 low-power sleep mode (0.2 uA)
    Serial.println("[LoRa] [P2P] Transceiver entered Deep Sleep mode.");
#endif
}

void LoRaManager::sleepWAN() {
#if ENABLE_PHYSICAL_LORAWAN
    // In LoRaWAN LMIC stack, we put transceiver to sleep
    radio_sleep();
    Serial.println("[LoRa] [LoRaWAN] Transceiver entered Sleep mode.");
#endif
}
