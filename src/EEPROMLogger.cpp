#include "EEPROMLogger.h"

EEPROMLogger::EEPROMLogger() : 
    _addr(AT24C32_I2C_ADDR), 
    _nextWriteIndex(0), 
    _isInitialized(false) {}

bool EEPROMLogger::begin(int sdaPin, int sclPin, uint32_t speed) {
    // Check if Wire has been started, if not initialize it
    static bool wireInitialized = false;
    if (!wireInitialized) {
        Wire.begin(sdaPin, sclPin, speed);
        wireInitialized = true;
    }

    // Ping EEPROM to verify existence
    Wire.beginTransmission(_addr);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    _isInitialized = true;
    findWriteHead();
    return true;
}

uint8_t EEPROMLogger::calculateCRC8(const uint8_t *data, size_t len) {
    // Dallas/Maxim CRC8 algorithm (polynomial = 0x31, equivalent to 0x8C reflected)
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        uint8_t inbyte = data[i];
        for (uint8_t bit = 8; bit > 0; bit--) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

bool EEPROMLogger::writeBytes(uint16_t memAddr, const uint8_t *data, uint16_t len) {
    uint16_t written = 0;
    while (written < len) {
        uint16_t bytesToWrite = len - written;
        uint16_t pageOffset = (memAddr + written) % EEPROM_PAGE_SIZE;
        uint16_t spaceInPage = EEPROM_PAGE_SIZE - pageOffset;
        
        if (bytesToWrite > spaceInPage) {
            bytesToWrite = spaceInPage;
        }

        Wire.beginTransmission(_addr);
        Wire.write((uint8_t)((memAddr + written) >> 8));   // Address MSB
        Wire.write((uint8_t)((memAddr + written) & 0xFF));  // Address LSB
        
        for (uint16_t i = 0; i < bytesToWrite; i++) {
            Wire.write(data[written + i]);
        }
        
        if (Wire.endTransmission() != 0) {
            return false;
        }

        // Acknowledge Polling (Hardware wait logic)
        // Pings the EEPROM address until it responds (indicating write cycle completed)
        unsigned long start = millis();
        bool ok = false;
        while (millis() - start < 15) {
            Wire.beginTransmission(_addr);
            if (Wire.endTransmission() == 0) {
                ok = true;
                break;
            }
            delay(1);
        }
        if (!ok) return false;

        written += bytesToWrite;
    }
    return true;
}

bool EEPROMLogger::readBytes(uint16_t memAddr, uint8_t *dest, uint16_t len) {
    Wire.beginTransmission(_addr);
    Wire.write((uint8_t)(memAddr >> 8));
    Wire.write((uint8_t)(memAddr & 0xFF));
    if (Wire.endTransmission() != 0) {
        return false;
    }

    uint16_t readBytesCount = 0;
    while (readBytesCount < len) {
        uint16_t chunk = len - readBytesCount;
        if (chunk > 32) {
            chunk = 32; // Limit standard I2C request size to standard buffers
        }
        
        uint8_t received = Wire.requestFrom(_addr, (uint8_t)chunk);
        if (received < chunk) {
            return false;
        }
        
        for (uint16_t i = 0; i < chunk; i++) {
            dest[readBytesCount + i] = Wire.read();
        }
        readBytesCount += chunk;
    }
    return true;
}

void EEPROMLogger::findWriteHead() {
    uint32_t maxTimestamp = 0;
    int maxIndex = -1;
    bool foundEmpty = false;
    int firstEmptyIndex = -1;

    for (uint16_t i = 0; i < EEPROM_MAX_RECORDS; i++) {
        SQMDataPacket pkt;
        uint16_t addr = i * EEPROM_RECORD_SIZE;
        
        if (readBytes(addr, (uint8_t*)&pkt, sizeof(SQMDataPacket))) {
            // Check if page is completely unwritten (unprogrammed EEPROM bytes default to 0xFF)
            if (pkt.timestamp == 0xFFFFFFFF) {
                if (!foundEmpty) {
                    firstEmptyIndex = i;
                    foundEmpty = true;
                }
                continue;
            }

            // Verify checksum integrity
            uint8_t calcCrc = calculateCRC8((uint8_t*)&pkt, sizeof(SQMDataPacket) - 1);
            if (pkt.checksum == calcCrc && pkt.timestamp > 0) {
                // If record is valid, look for the most recent timestamp
                if (pkt.timestamp > maxTimestamp) {
                    maxTimestamp = pkt.timestamp;
                    maxIndex = i;
                }
            } else {
                // If packet checksum fails, treat as an empty/overwritable slot
                if (!foundEmpty) {
                    firstEmptyIndex = i;
                    foundEmpty = true;
                }
            }
        }
    }

    if (maxIndex != -1) {
        // Active head is located immediately after the most recent valid record (modulo EEPROM size)
        _nextWriteIndex = (maxIndex + 1) % EEPROM_MAX_RECORDS;
    } else if (firstEmptyIndex != -1) {
        // No valid packets found, but empty/corrupt slots exist. Start there.
        _nextWriteIndex = firstEmptyIndex;
    } else {
        // Fallback to start
        _nextWriteIndex = 0;
    }
}

bool EEPROMLogger::logPacket(SQMDataPacket &packet) {
    if (!_isInitialized) return false;

    // Calculate CRC8 and store in the final byte of the packet structure
    packet.checksum = calculateCRC8((const uint8_t *)&packet, sizeof(SQMDataPacket) - 1);

    // Get physical write address (each record has a static 32-byte page spacing)
    uint16_t writeAddr = _nextWriteIndex * EEPROM_RECORD_SIZE;

    // Write packet to EEPROM
    if (!writeBytes(writeAddr, (const uint8_t *)&packet, sizeof(SQMDataPacket))) {
        return false;
    }

    // Increment head index and rollover if past limit (circular ring buffer)
    _nextWriteIndex = (_nextWriteIndex + 1) % EEPROM_MAX_RECORDS;
    return true;
}

bool EEPROMLogger::readPacket(uint16_t index, SQMDataPacket &packet) {
    if (!_isInitialized || index >= EEPROM_MAX_RECORDS) return false;

    uint16_t readAddr = index * EEPROM_RECORD_SIZE;
    if (!readBytes(readAddr, (uint8_t *)&packet, sizeof(SQMDataPacket))) {
        return false;
    }

    // Verify CRC8 integrity
    uint8_t calcCrc = calculateCRC8((const uint8_t *)&packet, sizeof(SQMDataPacket) - 1);
    return (packet.checksum == calcCrc);
}

bool EEPROMLogger::wipeEEPROM() {
    if (!_isInitialized) return false;

    uint8_t wipeBuf[EEPROM_RECORD_SIZE];
    memset(wipeBuf, 0xFF, EEPROM_RECORD_SIZE);

    for (uint16_t i = 0; i < EEPROM_MAX_RECORDS; i++) {
        uint16_t addr = i * EEPROM_RECORD_SIZE;
        if (!writeBytes(addr, wipeBuf, EEPROM_RECORD_SIZE)) {
            return false;
        }
    }

    _nextWriteIndex = 0;
    return true;
}

void EEPROMLogger::dumpLogsToSerial() {
    if (!_isInitialized) {
        Serial.println("EEPROM Logger not initialized.");
        return;
    }

    Serial.println("--- START EEPROM DATA DUMP (CSV) ---");
    Serial.println("Index,Timestamp,MPSAS,Temp_C,Press_hPa,Hum_Pct,Mag_X,Mag_Y,Mag_Z,Batt_mV,CRC_OK");

    SQMDataPacket pkt;
    for (uint16_t i = 0; i < EEPROM_MAX_RECORDS; i++) {
        uint16_t readAddr = i * EEPROM_RECORD_SIZE;
        if (readBytes(readAddr, (uint8_t *)&pkt, sizeof(SQMDataPacket))) {
            if (pkt.timestamp == 0xFFFFFFFF || pkt.timestamp == 0) {
                continue; // Unprogrammed/empty slot
            }

            uint8_t calcCrc = calculateCRC8((const uint8_t *)&pkt, sizeof(SQMDataPacket) - 1);
            bool crcOk = (pkt.checksum == calcCrc);

            Serial.printf("%d,%u,%.4f,%.2f,%.2f,%.2f,%d,%d,%d,%u,%s\n",
                i,
                pkt.timestamp,
                pkt.sqm_reading,
                (float)pkt.amb_temp / 100.0f,
                (float)pkt.amb_press,
                (float)pkt.amb_hum / 100.0f,
                pkt.mag_x,
                pkt.mag_y,
                pkt.mag_z,
                pkt.batt_mv,
                crcOk ? "YES" : "NO"
            );
        }
    }
    Serial.println("--- END EEPROM DATA DUMP ---");
}
