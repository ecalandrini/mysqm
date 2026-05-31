#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==========================================
// FEATURE TOGGLE SWITCHES (Compile-Time HAL)
// Comment out any block to completely compile out its drivers and libraries.
// ==========================================
#define ENABLE_BME280      // Adafruit BME280 Environment Sensor (Temp/Press/Hum)
#define ENABLE_TSL2591     // Adafruit TSL2591 Light Sensor (Sky Quality MPSAS)
#define ENABLE_MLX90614    // Melexis MLX90614 Infrared Sky Temp Sensor (Cloud Cover)
#define ENABLE_BNO055      // Adafruit BNO055 IMU/Tilt Sensor (Zenith Leveling)
#define ENABLE_HMC5883L    // Honeywell HMC5883L Magnetometer (Pointing Heading)
#define ENABLE_INA3221     // TI INA3221 Triple-Channel Shunt Monitor (Battery Level)
#define ENABLE_AT24C32     // I2C 32Kbit EEPROM Module (Non-Volatile Ring Buffer Logging)
#define ENABLE_GPS         // NEO-M8 UART GPS Sensor (Location / Clock Sync)
#define ENABLE_OLED        // I2C SSD1306 OLED Display & Interactive USER button interface

// Communication Channel Configuration
enum CommMethod {
    COMM_METHOD_LORA,
    COMM_METHOD_WIFI
};
// Configure the active transmission channel (COMM_METHOD_LORA or COMM_METHOD_WIFI)
#define ACTIVE_COMM_METHOD   COMM_METHOD_LORA 

// WiFi Network Credentials & Settings (Active when COMM_METHOD_WIFI is configured)
#define WIFI_SSID            "Your_Router_SSID"
#define WIFI_PASSWORD        "Your_Router_Password"
#define WIFI_HTTP_POST_URL   "http://api.skyquality.net/sqm/log" // Target server API endpoint
#define WIFI_CONN_TIMEOUT_S  10   // 10-second timeout to connect to WiFi before cycle skips

// I2C Bus Settings
#define I2C_SDA_PIN          21
#define I2C_SCL_PIN          22
#define I2C_BUS_SPEED        100000 // 100 kHz for stability at 3.3V with AT24C32

// Sensor & Display I2C Addresses (Defined only if hardware is enabled)
#ifdef ENABLE_INA3221
#define INA3221_I2C_ADDR     0x40 
#endif

#ifdef ENABLE_HMC5883L
#define HMC5883L_I2C_ADDR    0x1E 
#endif

#ifdef ENABLE_AT24C32
#define AT24C32_I2C_ADDR     0x50 
#endif

#ifdef ENABLE_TSL2591
#define TSL2591_I2C_ADDR     0x29 
#endif

#ifdef ENABLE_BME280
#define BME280_I2C_ADDR      0x77 
#endif

#ifdef ENABLE_MLX90614
#define MLX90614_I2C_ADDR    0x5A 
#endif

#ifdef ENABLE_BNO055
#define BNO055_I2C_ADDR      0x28 
#endif

#ifdef ENABLE_OLED
#define SSD1306_I2C_ADDR     0x3C 
#endif

// Hardware Interfacing Pins
#ifdef ENABLE_HMC5883L
#define HMC5883L_DRDY_PIN    15   // Hardware Data Ready Pin
#endif

// GPS Module Integration (Hardware Serial2 UART)
#ifdef ENABLE_GPS
#define GPS_RX_PIN           16   // ESP32 Serial2 RX pin (connect to GPS TX)
#define GPS_TX_PIN           17   // ESP32 Serial2 TX pin (connect to GPS RX)
#define GPS_BAUD_RATE        9600 // Standard baud rate for Neo-6M/8M GPS receivers
// GPIO to toggle a MOSFET/transistor for GPS power shutdown. 
// Set to -1 if GPS module is permanently powered.
#define GPS_POWER_ENABLE_PIN -1   
#endif

// User Setup Configuration Interface
#ifdef ENABLE_OLED
#define USER_BUTTON_PIN      12   // Physical installation & menu button (RTC capable pin)
#define SETUP_MODE_TIMEOUT_S 180  // Setup Mode idle timeout in seconds (3 minutes)
#endif

// Operational Modes Settings
#define ECLIPSE_SWITCH_PIN   13   // Hardware toggle switch for Eclipse Mode (pulled LOW is active)
#define ECLIPSE_INTERVAL_MS  2000 // 2-second interval in milliseconds during eclipse
#define LOG_ECLIPSE_TO_EEPROM false // Set true to write 2s samples to EEPROM (warn: high wear!)

// SQM Calibration Zero Point
#define SQM_ZERO_POINT       21.0f

// Logging & EEPROM Parameters (AT24C32 has 32-byte pages, 32 Kbits = 4096 bytes total)
#ifdef ENABLE_AT24C32
#define EEPROM_SIZE          4096
#define EEPROM_PAGE_SIZE     32
#define EEPROM_RECORD_SIZE   32 
#define EEPROM_MAX_RECORDS   120  // Reduced from 128 to reserve 256 bytes for calibration
#define EEPROM_BNO_CAL_ADDR  3840 // 22 bytes reserved for BNO055 calibration offset structure
#define EEPROM_HMC_CAL_ADDR  3870 // 4 bytes reserved for HMC5883L STP calibration
#endif

// System Power Management (Deep Sleep durations for Day / Night)
#define DEEP_SLEEP_DAY_S     300  // 5 minutes wake interval (300 seconds) in Day Mode
#define DEEP_SLEEP_NIGHT_S   180  // 3 minutes wake interval (180 seconds) in Night Mode

#ifdef ENABLE_GPS
#define GPS_SYNC_TIMEOUT_MS  60000 // 60-second limit to wait for a satellite fix on boot
#endif

// Serial Communication
#define SERIAL_BAUD_RATE     115200

// Git Versioning Fallback (Overridden dynamically by PlatformIO build flags)
#ifndef GIT_VERSION
#define GIT_VERSION "v1.0.0-manual"
#endif

#endif // CONFIG_H
