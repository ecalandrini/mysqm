#ifndef HMC5883L_H
#define HMC5883L_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

class HMC5883L {
public:
    HMC5883L();

    // Initializes I2C connection and checks device ID registers
    bool begin(int sdaPin = I2C_SDA_PIN, int sclPin = I2C_SCL_PIN, uint32_t speed = I2C_BUS_SPEED);

    // Writes configuration registers A, B, and Mode
    bool configure(uint8_t cra, uint8_t crb, uint8_t mode);

    // Set normal operation (continuous mode, 8-sample avg, 15Hz ODR, Gain 5)
    bool setNormalOperation();

    // Performs self-test by applying positive bias and returns raw axis readings
    bool performSelfTest(int16_t &rawX, int16_t &rawY, int16_t &rawZ);

    // Sets the factory calibration self-test baseline (average of axes during factory self-test)
    void setFactorySTP(float stpCal);

    // Gets the current sensitivity coefficient in LSb/Gauss
    float getSensitivityLSb() const;

    // Triggers and polls a single measurement (Single-Measurement Mode), returns raw X, Y, Z
    bool readRaw(int16_t &rawX, int16_t &rawY, int16_t &rawZ);

    // Performs measurement and returns compensated values in Gauss (applies gain scaling and temperature compensation)
    bool readCompensated(float &gaussX, float &gaussY, float &gaussZ);

    // Calculates tilt compensation using pitch and roll from BNO055 (in radians)
    void getTiltCompensatedHeading(float gaussX, float gaussY, float gaussZ, float pitchRad, float rollRad, float &headingDeg);

private:
    uint8_t _addr;
    uint8_t _cra;
    uint8_t _crb;
    uint8_t _mode;
    float _stpCal;       // Factory self-test STP calibration value
    bool _isInitialized;

    // Writes a byte to a register
    bool writeRegister(uint8_t reg, uint8_t val);

    // Reads sequential bytes from a starting register
    bool readRegisters(uint8_t reg, uint8_t *dest, uint8_t count);
};

#endif // HMC5883L_H
