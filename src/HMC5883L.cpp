#include "HMC5883L.h"

HMC5883L::HMC5883L() : 
    _addr(HMC5883L_I2C_ADDR), 
    _cra(0x70), 
    _crb(0xA0), 
    _mode(0x00), 
    _stpCal(0.0f), 
    _isInitialized(false) {}

bool HMC5883L::begin(int sdaPin, int sclPin, uint32_t speed) {
    // Check if Wire has been started, if not initialize it
    // Under ESP32 we can configure custom pins and I2C frequency
    static bool wireInitialized = false;
    if (!wireInitialized) {
        Wire.begin(sdaPin, sclPin, speed);
        wireInitialized = true;
    }

    // Verify connection by reading ID registers (0x0A, 0x0B, 0x0C)
    // Identification Register A should be 'H' (0x48)
    // Identification Register B should be '4' (0x34)
    // Identification Register C should be '3' (0x33)
    uint8_t id[3] = {0};
    if (!readRegisters(0x0A, id, 3)) {
        return false;
    }

    if (id[0] != 0x48 || id[1] != 0x34 || id[2] != 0x33) {
        return false;
    }

    _isInitialized = true;
    return setNormalOperation();
}

bool HMC5883L::writeRegister(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

bool HMC5883L::readRegisters(uint8_t reg, uint8_t *dest, uint8_t count) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    uint8_t received = Wire.requestFrom(_addr, count);
    if (received < count) {
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        dest[i] = Wire.read();
    }
    return true;
}

bool HMC5883L::configure(uint8_t cra, uint8_t crb, uint8_t mode) {
    if (!_isInitialized) return false;

    if (!writeRegister(0x00, cra)) return false;
    _cra = cra;

    if (!writeRegister(0x01, crb)) return false;
    _crb = crb;

    if (!writeRegister(0x02, mode)) return false;
    _mode = mode;

    return true;
}

bool HMC5883L::setNormalOperation() {
    // Register A: 0x70 -> 8-sample average, 15 Hz output rate, normal measurement configuration
    // Register B: 0xA0 -> Gain 5 (±4.7 Ga range, 390 LSb/Gauss sensitivity)
    // Mode: 0x00 -> Continuous-measurement mode
    return configure(0x70, 0xA0, 0x00);
}

float HMC5883L::getSensitivityLSb() const {
    // Gain field is in bits 7:5 of Register B (_crb)
    uint8_t gain = (_crb >> 5) & 0x07;
    switch (gain) {
        case 0: return 1370.0f; // ±0.88 Ga
        case 1: return 1090.0f; // ±1.30 Ga
        case 2: return 820.0f;  // ±1.90 Ga
        case 3: return 660.0f;  // ±2.50 Ga
        case 4: return 440.0f;  // ±4.00 Ga
        case 5: return 390.0f;  // ±4.70 Ga (Default)
        case 6: return 330.0f;  // ±5.60 Ga
        case 7: return 230.0f;  // ±8.10 Ga
        default: return 390.0f;
    }
}

bool HMC5883L::performSelfTest(int16_t &rawX, int16_t &rawY, int16_t &rawZ) {
    if (!_isInitialized) return false;

    // 1. Set CRA to 0x71 (Positive Bias mode, 8-sample avg, 15 Hz ODR)
    uint8_t prevCRA = _cra;
    uint8_t prevMode = _mode;
    
    if (!writeRegister(0x00, 0x71)) return false;
    delay(60); // Allow sensor to settle and establish bias magnetic field (~56ms transition)

    // 2. Trigger a single-shot measurement and read the results
    if (!readRaw(rawX, rawY, rawZ)) {
        // Revert CRA on failure
        writeRegister(0x00, prevCRA);
        return false;
    }

    // 3. Revert CRA and Mode to normal operation
    writeRegister(0x00, prevCRA);
    writeRegister(0x02, prevMode);
    delay(10);

    // 4. Verification logic
    // At Gain 5, positive bias raw counts must be between 243 and 575 LSb on all axes
    float currentSens = getSensitivityLSb();
    float sensG5 = 390.0f;
    
    // Scale limits if currently not at Gain 5 (using the formula Limit_adj = Limit_G5 * Gain_new / Gain_G5)
    float minLimit = 243.0f * (currentSens / sensG5);
    float maxLimit = 575.0f * (currentSens / sensG5);

    bool xOk = (rawX >= minLimit && rawX <= maxLimit);
    bool yOk = (rawY >= minLimit && rawY <= maxLimit);
    bool zOk = (rawZ >= minLimit && rawZ <= maxLimit);

    return (xOk && yOk && zOk);
}

void HMC5883L::setFactorySTP(float stpCal) {
    _stpCal = stpCal;
}

bool HMC5883L::readRaw(int16_t &rawX, int16_t &rawY, int16_t &rawZ) {
    if (!_isInitialized) return false;

    // 1. Trigger single measurement by writing 0x01 to Mode Register (0x02)
    if (!writeRegister(0x02, 0x01)) return false;

    // 2. Poll Status Register (0x09) bit 0 (RDY) or hardware pin
    bool dataReady = false;
    unsigned long startTime = millis();
    const unsigned long timeout = 150; // Max execution time for single measurement at 15Hz (~67ms)

    if (HMC5883L_DRDY_PIN >= 0) {
        pinMode(HMC5883L_DRDY_PIN, INPUT);
        while (millis() - startTime < timeout) {
            if (digitalRead(HMC5883L_DRDY_PIN) == HIGH) {
                dataReady = true;
                break;
            }
            delay(1);
        }
    }

    // Fallback or explicit check of Status Register
    if (!dataReady) {
        while (millis() - startTime < timeout) {
            uint8_t status = 0;
            if (readRegisters(0x09, &status, 1)) {
                if (status & 0x01) { // RDY bit
                    dataReady = true;
                    break;
                }
            }
            delay(1);
        }
    }

    if (!dataReady) {
        return false;
    }

    // 3. Read 6 sequential bytes (0x03 to 0x08)
    uint8_t buf[6] = {0};
    if (!readRegisters(0x03, buf, 6)) {
        return false;
    }

    // 4. Endianness manual reconstruction (Big-Endian MSB first)
    // 5. Correct register ordering: X (0x03-0x04), Z (0x05-0x06), Y (0x07-0x08)
    rawX = (int16_t)((buf[0] << 8) | buf[1]);
    rawZ = (int16_t)((buf[2] << 8) | buf[3]);
    rawY = (int16_t)((buf[4] << 8) | buf[5]);

    // Guard against overflow values (HMC5883L outputs -4096 when overflow occurs)
    if (rawX == -4096 || rawY == -4096 || rawZ == -4096) {
        return false;
    }

    return true;
}

bool HMC5883L::readCompensated(float &gaussX, float &gaussY, float &gaussZ) {
    int16_t rawX = 0, rawY = 0, rawZ = 0;
    if (!readRaw(rawX, rawY, rawZ)) {
        return false;
    }

    // Calculate current STP (self-test output magnitude) for temperature drift compensation
    // In order to perform the sensitivity compensation at runtime, we compute the offset scale factor.
    // If factory calibration STP is registered, we scale the outputs.
    float xComp = 1.0f;
    if (_stpCal > 0.0f) {
        int16_t testX = 0, testY = 0, testZ = 0;
        // Trigger self test measurement to read current positive bias magnitude
        if (performSelfTest(testX, testY, testZ)) {
            float stpCurr = sqrt((float)testX * testX + (float)testY * testY + (float)testZ * testZ);
            if (stpCurr > 0.0f) {
                xComp = _stpCal / stpCurr;
            }
        }
    }

    // Gain scaling factor: converts raw counts to Gauss (1 / LSb per Gauss)
    float sensLSb = getSensitivityLSb();
    gaussX = ((float)rawX / sensLSb) * xComp;
    gaussY = ((float)rawY / sensLSb) * xComp;
    gaussZ = ((float)rawZ / sensLSb) * xComp;

    return true;
}

void HMC5883L::getTiltCompensatedHeading(float gaussX, float gaussY, float gaussZ, float pitchRad, float rollRad, float &headingDeg) {
    float cosPitch = cos(pitchRad);
    float sinPitch = sin(pitchRad);
    float cosRoll = cos(rollRad);
    float sinRoll = sin(rollRad);

    // Standard equations for pitch (theta) and roll (phi) tilt compensation
    // Maps the 3D magnetometer vector into the flat horizontal plane
    float xH = gaussX * cosPitch + gaussY * sinPitch * sinRoll + gaussZ * sinPitch * cosRoll;
    float yH = gaussY * cosRoll - gaussZ * sinRoll;

    // Calculate heading in degrees (pointing angle)
    headingDeg = atan2(-yH, xH) * 180.0f / M_PI;
    if (headingDeg < 0) {
        headingDeg += 360.0f;
    }
}
