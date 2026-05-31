#ifndef EEPROM_LOGGER_H
#define EEPROM_LOGGER_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// The packed data packet structure (23 bytes)
struct __attribute__((packed)) SQMDataPacket {
    uint32_t timestamp;      // Epoch time (4 bytes)
    float sqm_reading;       // MPSAS value (4 bytes)
    int16_t amb_temp;        // BME280 Temp * 100 (2 bytes)
    uint16_t amb_press;      // BME280 Pressure (2 bytes)
    uint16_t amb_hum;        // BME280 Humidity * 100 (2 bytes)
    int16_t mag_x;           // HMC5883L Raw X (2 bytes)
    int16_t mag_y;           // HMC5883L Raw Y (2 bytes)
    int16_t mag_z;           // HMC5883L Raw Z (2 bytes)
    uint16_t batt_mv;        // INA3221 Bus Voltage (2 bytes)
    uint8_t checksum;        // CRC8 of previous 22 bytes (1 byte)
};

class EEPROMLogger {
public:
    EEPROMLogger();

    // Initializes connection and scans EEPROM to determine the next write pointer
    bool begin(int sdaPin = I2C_SDA_PIN, int sclPin = I2C_SCL_PIN, uint32_t speed = I2C_BUS_SPEED);

    // Calculates Dallas/Maxim CRC8 over the data buffer
    static uint8_t calculateCRC8(const uint8_t *data, size_t len);

    // Appends a packet to the EEPROM (computes CRC8, performs page-aligned write)
    bool logPacket(SQMDataPacket &packet);

    // Reads a packet at the specified index (0 to 127)
    bool readPacket(uint16_t index, SQMDataPacket &packet);

    // Completely wipes all EEPROM records (fills with 0xFF)
    bool wipeEEPROM();

    // Prints all valid stored records to Serial in CSV format
    void dumpLogsToSerial();

    // Returns current write index
    uint16_t getWriteIndex() const { return _nextWriteIndex; }

    // Writes raw bytes to an EEPROM memory address (handles page alignment)
    bool writeBytes(uint16_t memAddr, const uint8_t *data, uint16_t len);

    // Reads raw bytes from an EEPROM memory address
    bool readBytes(uint16_t memAddr, uint8_t *dest, uint16_t len);

private:
    uint8_t _addr;
    uint16_t _nextWriteIndex;
    bool _isInitialized;

    // Scans the EEPROM to find the next write slot based on timestamps
    void findWriteHead();
};

#endif // EEPROM_LOGGER_H
