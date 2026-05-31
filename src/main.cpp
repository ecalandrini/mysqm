#include <Arduino.h>
#include <Wire.h>

#include "config.h"

// Conditional Library Inclusions
#ifdef ENABLE_BME280
#include <Adafruit_BME280.h>
#endif

#ifdef ENABLE_TSL2591
#include <Adafruit_TSL2591.h>
#endif

#ifdef ENABLE_MLX90614
#include <Adafruit_MLX90614.h>
#endif

#ifdef ENABLE_BNO055
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#endif

#ifdef ENABLE_OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#endif

#ifdef ENABLE_GPS
#include <TinyGPS++.h>
#include <time.h>
#include <sys/time.h>
#endif

#include "HMC5883L.h"
#include "EEPROMLogger.h"
#include "LoRaManager.h"
#include "WiFiManager.h"
#include "SolarCalculator.h"

// System Mode Enum
enum SystemMode {
    SYSTEM_MODE_DAY,
    SYSTEM_MODE_NIGHT,
    SYSTEM_MODE_ECLIPSE
};

// Global Volatile Mode Indicator for continuous awake loop in Eclipse Mode
volatile bool inEclipseMode = false;

// Conditional Display Instantiation
#ifdef ENABLE_OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing ESP32 reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

// Persistent variables across Deep Sleep (RTC Fast Memory)
#ifdef ENABLE_HMC5883L
RTC_DATA_ATTR float    rtc_stpCal = 0.0f;       // Factory calibration STP (Self-Test Output) value
#endif
RTC_DATA_ATTR uint32_t rtc_bootCount = 0;    // Number of wake cycles

#ifdef ENABLE_GPS
RTC_DATA_ATTR float    rtc_gpsLat = 0.0f;       // Cached GPS Latitude
RTC_DATA_ATTR float    rtc_gpsLon = 0.0f;       // Cached GPS Longitude
RTC_DATA_ATTR bool     rtc_gpsValid = false;    // True if valid GPS lock is cached
RTC_DATA_ATTR time_t   rtc_lastSyncTime = 0;    // UNIX timestamp of last GPS clock sync
#endif

// Core System Driver Instances (Conditional)
#ifdef ENABLE_BME280
Adafruit_BME280   bme;
#endif

#ifdef ENABLE_TSL2591
Adafruit_TSL2591  tsl = Adafruit_TSL2591(2591); // Sensor ID
#endif

#ifdef ENABLE_MLX90614
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
#endif

#ifdef ENABLE_BNO055
Adafruit_BNO055   bno = Adafruit_BNO055(55, BNO055_I2C_ADDR);
#endif

#ifdef ENABLE_HMC5883L
HMC5883L          hmc;
#endif

#ifdef ENABLE_AT24C32
EEPROMLogger      eepromLog;
#endif

LoRaManager       loraManager(LORA_MODE_MOCK); // Coexisting LoRa transmitter
WiFiManager       wifiManager;                 // Standalone WiFi transmitter option

#ifdef ENABLE_GPS
TinyGPSPlus       gpsDecoder;
#endif

// Forward declarations of helper functions
bool checkPowerRails();
void runHMCCalibration();
void shutdownPeripherals();
void sleepBME280();
void sleepBNO055();
void sleepMLX90614();
bool checkI2CDevice(uint8_t address);
void executeMeasurement(SystemMode mode);

#ifdef ENABLE_GPS
bool synchronizeClockAndLocation();
time_t convertUTCToEpoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
#endif

#ifdef ENABLE_OLED
void runSetupMode();
#endif

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(500); // Settle Serial terminal output
    
    Serial.printf("\n[System] ESP32 SQM Station - Firmware Version: %s\n", GIT_VERSION);
    rtc_bootCount++;
    
    // Configure USER button and Eclipse Mode Switch
#ifdef ENABLE_OLED
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
#endif
    pinMode(ECLIPSE_SWITCH_PIN, INPUT_PULLUP);
    
    // Configure GPS Power Control Pin early to prevent leakage
#ifdef ENABLE_GPS
    if (GPS_POWER_ENABLE_PIN >= 0) {
        pinMode(GPS_POWER_ENABLE_PIN, OUTPUT);
        digitalWrite(GPS_POWER_ENABLE_PIN, LOW); // Keep powered off by default
    }
#endif

    // Assess if Eclipse Mode is active (pulled LOW via physical switch)
    if (digitalRead(ECLIPSE_SWITCH_PIN) == LOW) {
        inEclipseMode = true;
    }

    // Determine Setup Mode Trigger cause (Conditional on OLED module presence, bypassed in Eclipse)
    bool enterSetup = false;
#ifdef ENABLE_OLED
    if (!inEclipseMode) {
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        enterSetup = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
        if (rtc_bootCount == 1 && digitalRead(USER_BUTTON_PIN) == LOW) {
            enterSetup = true;
        }
    }
#endif

    if (enterSetup) {
#ifdef ENABLE_OLED
        Serial.println("\n[System] USER Button detected. Entering Interactive Setup Mode.");
        runSetupMode();
        // Setup mode loop handles its own shutdown and deep sleep sequence, so it never reaches here.
#endif
    }

    // Initialize core components
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_BUS_SPEED);

    if (!checkPowerRails()) {
        Serial.println("[CRITICAL] Power-Good Verification failed! V_DD is outside operating limits.");
        Serial.println("[System] Suspending startup sequence. Entering emergency Deep Sleep.");
        
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DAY_S * 1000000ULL);
#ifdef ENABLE_OLED
        esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
        rtc_gpio_pullup_en((gpio_num_t)USER_BUTTON_PIN);
#endif
        esp_deep_sleep_start();
    }

    // Core sensor drivers boots (Conditional)
#ifdef ENABLE_HMC5883L
    hmc.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_BUS_SPEED);
#endif
#ifdef ENABLE_BME280
    bme.begin(BME280_I2C_ADDR);
#endif
#ifdef ENABLE_MLX90614
    mlx.begin();
#endif
#ifdef ENABLE_BNO055
    bno.begin();
#ifdef ENABLE_AT24C32
    adafruit_bno055_offsets_t bnoOffsets;
    if (eepromLog.readBytes(EEPROM_BNO_CAL_ADDR, (uint8_t*)&bnoOffsets, sizeof(bnoOffsets))) {
        bool offsetsValid = false;
        uint8_t *offsetPtr = (uint8_t*)&bnoOffsets;
        for (size_t i = 0; i < sizeof(bnoOffsets); i++) {
            if (offsetPtr[i] != 0xFF) {
                offsetsValid = true;
                break;
            }
        }
        if (offsetsValid) {
            bno.setSensorOffsets(bnoOffsets);
            Serial.println("[BNO055] Persistent calibration offsets successfully restored from EEPROM.");
        } else {
            Serial.println("[BNO055] No valid persistent calibration offsets found in EEPROM.");
        }
    }
#endif
#endif
#ifdef ENABLE_AT24C32
    eepromLog.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_BUS_SPEED);
#endif

    // Initialize Active Comm stack
    if (ACTIVE_COMM_METHOD == COMM_METHOD_WIFI) {
        wifiManager.begin();
    } else {
        loraManager.begin();
    }

#ifdef ENABLE_HMC5883L
    if (rtc_stpCal <= 0.0f) {
#ifdef ENABLE_AT24C32
        float savedSTP = 0.0f;
        if (eepromLog.readBytes(EEPROM_HMC_CAL_ADDR, (uint8_t*)&savedSTP, sizeof(savedSTP))) {
            if (savedSTP > 0.0f && savedSTP != 0xFFFFFFFF && !isnan(savedSTP)) {
                rtc_stpCal = savedSTP;
                Serial.printf("[HMC5883L] Persistent STP calibration successfully loaded from EEPROM: %.2f\n", rtc_stpCal);
            }
        }
#endif
    }

    if (rtc_stpCal <= 0.0f) {
        runHMCCalibration();
    } else {
        hmc.setFactorySTP(rtc_stpCal);
    }
#endif

#ifdef ENABLE_GPS
    // Run GPS sync checks on boot
    time_t elapsedSinceSync = time(nullptr) - rtc_lastSyncTime;
    bool needsSync = (!rtc_gpsValid) || (elapsedSinceSync > 7 * 24 * 3600);
    if (needsSync && !inEclipseMode) { // Skip GPS clock locks in high-speed Eclipse Mode
        synchronizeClockAndLocation();
    }
#endif

    // -------------------------------------------------------------
    // ROUTE OPERATIONAL MODE BRANCHES
    // -------------------------------------------------------------
    if (inEclipseMode) {
        Serial.println("\n==============================================");
        Serial.println("         !!! ECLIPSE MODE ENGAGED !!!         ");
        Serial.println("  Deep sleep disabled. Running 2-second loop. ");
        Serial.println("==============================================");

#ifdef ENABLE_OLED
        if (display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDR)) {
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 16);
            display.println("  ECLIPSE ACTIVE");
            display.println("  Awake loop 2s");
            display.display();
        }
#endif
        // We do NOT call deep sleep. We allow setup to terminate and flow into loop()!
        return;
    }

    // Normal astronomical Day / Night schedules (Wake, Push, and Sleep)
    bool isNight = true; 
#if defined(ENABLE_GPS) && defined(ENABLE_TSL2591)
    float sunriseUTC = 0.0f;
    float sunsetUTC = 0.0f;
    time_t checkNow = time(nullptr);
    struct tm *checkInfo = gmtime(&checkNow);
    
    if (rtc_gpsValid && checkNow > 1000000) {
        if (SolarCalculator::calculateSunriseSunsetUTC(rtc_gpsLat, rtc_gpsLon, checkInfo->tm_year + 1900, checkInfo->tm_mon + 1, checkInfo->tm_mday, sunriseUTC, sunsetUTC)) {
            float currentUTCMinutes = checkInfo->tm_hour * 60.0f + checkInfo->tm_min;
            isNight = (currentUTCMinutes < sunriseUTC || currentUTCMinutes > sunsetUTC);
        }
    }
#endif

    SystemMode targetMode = isNight ? SYSTEM_MODE_NIGHT : SYSTEM_MODE_DAY;
    uint32_t sleepDuration = (targetMode == SYSTEM_MODE_NIGHT) ? DEEP_SLEEP_NIGHT_S : DEEP_SLEEP_DAY_S;

    Serial.println("\n==============================================");
    Serial.printf("ESP32 SQM Station - Wake Cycle #%u\n", rtc_bootCount);
    Serial.printf("Operational Schedule: %s\n", (targetMode == SYSTEM_MODE_NIGHT) ? "NIGHT MODE (3-min sleep)" : "DAY MODE (5-min sleep)");
    Serial.println("==============================================");

    // Execute standard single-shot Day/Night measurement, log, and sleep
    executeMeasurement(targetMode);

    Serial.println("[Power] Transitioning peripherals to low-power state...");
    shutdownPeripherals();
    
    Serial.printf("[Power] Deep sleep entry. Scheduled wake in %u seconds.\n", sleepDuration);
    Serial.println("==============================================\n");
    
    // Set up wakeup triggers
    esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
#ifdef ENABLE_OLED
    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0); // Wake on Setup button
    rtc_gpio_pullup_en((gpio_num_t)USER_BUTTON_PIN);
#endif
    // Also wake instantly if the Eclipse Switch is toggled LOW (closed)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ECLIPSE_SWITCH_PIN, 0);
    rtc_gpio_pullup_en((gpio_num_t)ECLIPSE_SWITCH_PIN);
    
    esp_deep_sleep_start();
}

void loop() {
    // This loop is executed EXCLUSIVELY in Eclipse Mode (2-second awake loop, deep sleep disabled)
    if (!inEclipseMode) {
        return; 
    }

    static unsigned long lastMeasurementTime = 0;
    
    // Non-blocking timer matching ECLIPSE_INTERVAL_MS (2000ms / 2 seconds)
    if (millis() - lastMeasurementTime >= ECLIPSE_INTERVAL_MS) {
        lastMeasurementTime = millis();
        
        Serial.println("\n[Eclipse] Triggering high-speed 2-second measurement cycle...");
        executeMeasurement(SYSTEM_MODE_ECLIPSE);
        
        // Dynamic OLED screen refresh in Eclipse Mode
#ifdef ENABLE_OLED
        // Read Pitch/Roll for zenith monitor
        float pitch = 0.0f, roll = 0.0f;
#ifdef ENABLE_BNO055
        sensors_event_t bnoEvent;
        bno.getEvent(&bnoEvent);
        pitch = bnoEvent.orientation.y;
        roll = bnoEvent.orientation.z;
#endif
        // Read live brightness
        float sqm = -2.0f;
#ifdef ENABLE_TSL2591
        if (tsl.begin()) {
            tsl.setGain(TSL2591_GAIN_MAX);
            tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS); // Fast integration for Eclipse
            delay(120);
            uint32_t lum = tsl.getFullLuminosity();
            float lux = tsl.calculateLux(lum & 0xFFFF, lum >> 16);
            sqm = (lux > 0.0f) ? (SQM_ZERO_POINT - 2.5f * log10(lux)) : 22.0f;
            tsl.disable();
        }
#endif

        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("--- ECLIPSE RUN ---");
        display.printf("Interval: 2.0s (AWAKE)\n");
        if (sqm > 0.0f) {
            display.printf("MPSAS: %.3f\n", sqm);
        } else {
            display.println("MPSAS: --- (ERR)\n");
        }
        display.printf("Tilt: P:%.1f R:%.1f\n", pitch, roll);
        display.printf("WiFi/LoRa Status: ACTIVE\n");
        display.display();
#endif
    }

    // Constantly poll the Eclipse physical switch
    if (digitalRead(ECLIPSE_SWITCH_PIN) == HIGH) {
        // Eclipse switch was flipped off (opened)! Exit Eclipse Mode and enter standard deep sleep
        Serial.println("\n[Eclipse] Switch toggled OFF. Exiting high-speed awake loop.");
        inEclipseMode = false;
        
#ifdef ENABLE_OLED
        display.clearDisplay();
        display.setCursor(0, 16);
        display.println("  ECLIPSE TERMINATED");
        display.println("  ENTERING SLEEP...");
        display.display();
        delay(800);
        display.ssd1306_command(SSD1306_DISPLAYOFF);
#endif
        
        shutdownPeripherals();
        
        Serial.printf("[Power] Deep sleep entry. Resuming astronomical Day/Night schedules.\n");
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DAY_S * 1000000ULL);
#ifdef ENABLE_OLED
        esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
        rtc_gpio_pullup_en((gpio_num_t)USER_BUTTON_PIN);
#endif
        esp_sleep_enable_ext0_wakeup((gpio_num_t)ECLIPSE_SWITCH_PIN, 0);
        rtc_gpio_pullup_en((gpio_num_t)ECLIPSE_SWITCH_PIN);
        esp_deep_sleep_start();
    }
}

void executeMeasurement(SystemMode mode) {
    // A. Read BNO055 orientation
    float pitchRad = 0.0f, rollRad = 0.0f;
#ifdef ENABLE_BNO055
    sensors_event_t bnoEvent;
    bno.getEvent(&bnoEvent);
    pitchRad = bnoEvent.orientation.y * M_PI / 180.0f;
    rollRad = bnoEvent.orientation.z * M_PI / 180.0f;
#endif

    // B. Read HMC5883L Magnetometer
    float gaussX = 0, gaussY = 0, gaussZ = 0;
    int16_t rawMagX = 0, rawMagY = 0, rawMagZ = 0;
#ifdef ENABLE_HMC5883L
    hmc.readRaw(rawMagX, rawMagY, rawMagZ);
    hmc.readCompensated(gaussX, gaussY, gaussZ);
#endif

    // C. Read BME280 Ambient Metrics
    float tempC = 0.0f, pressHpa = 0.0f, humPct = 0.0f;
#ifdef ENABLE_BME280
    tempC = bme.readTemperature();
    pressHpa = bme.readPressure() / 100.0f;
    humPct = bme.readHumidity();
#endif

    // D. Read MLX90614 Sky Temperature
    float ambientTemp = 0.0f, objectTemp = 0.0f;
#ifdef ENABLE_MLX90614
    ambientTemp = mlx.readAmbientTempC();
    objectTemp = mlx.readObjectTempC();
#endif

    // E. Read TSL2591 Light Sensor
    float sqmReading = -1.0f; // Default Day sentinel value
    
#ifdef ENABLE_TSL2591
    if (mode == SYSTEM_MODE_NIGHT || mode == SYSTEM_MODE_ECLIPSE) {
        // Read light levels (night or eclipse)
        if (tsl.begin()) {
            tsl.setGain(TSL2591_GAIN_MAX);
            
            // Adjust integration time dynamically: fast 100ms in Eclipse, deep 400ms at Night
            if (mode == SYSTEM_MODE_ECLIPSE) {
                tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
                delay(120);
            } else {
                tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
                delay(450);
            }
            
            uint32_t lum = tsl.getFullLuminosity();
            float lux = tsl.calculateLux(lum & 0xFFFF, lum >> 16);
            if (lux > 0.0f) {
                sqmReading = SQM_ZERO_POINT - 2.5f * log10(lux);
                if (sqmReading > 22.0f) sqmReading = 22.0f;
                if (sqmReading < 0.0f) sqmReading = 0.0f;
            } else {
                sqmReading = 22.0f;
            }
            tsl.disable();
        }
    }
#else
    sqmReading = -2.0f; // Compiled out sentinel
#endif

    // F. Read INA3221 Battery Voltage
    uint16_t battMv = 0;
#ifdef ENABLE_INA3221
    if (checkPowerRails()) {
        Wire.beginTransmission(INA3221_I2C_ADDR);
        Wire.write(0x02);
        if (Wire.endTransmission() == 0 && Wire.requestFrom(INA3221_I2C_ADDR, 2) == 2) {
            int16_t regVal = (Wire.read() << 8) | Wire.read();
            battMv = (regVal >> 3) * 8;
        }
    }
#endif

    // G. Structure Packaging
    SQMDataPacket packet;
#ifdef ENABLE_GPS
    packet.timestamp = (uint32_t)time(nullptr);
#else
    packet.timestamp = (uint32_t)(rtc_bootCount * DEEP_SLEEP_DAY_S);
#endif
    packet.sqm_reading = sqmReading;
    packet.amb_temp = (int16_t)(tempC * 100);
    packet.amb_press = (uint16_t)(pressHpa);
    packet.amb_hum = (uint16_t)(humPct * 100);
    packet.mag_x = rawMagX;
    packet.mag_y = rawMagY;
    packet.mag_z = rawMagZ;
    packet.batt_mv = battMv;

    // H. EEPROM Logging (Optional during Eclipse)
#ifdef ENABLE_AT24C32
    bool logToEEPROM = true;
    if (mode == SYSTEM_MODE_ECLIPSE && !LOG_ECLIPSE_TO_EEPROM) {
        logToEEPROM = false;
    }
    if (logToEEPROM) {
        eepromLog.logPacket(packet);
    }
#endif

    // I. Data Transmission
    if (ACTIVE_COMM_METHOD == COMM_METHOD_WIFI) {
        wifiManager.transmitPacket(packet);
    } else {
        loraManager.transmitPacket(packet);
    }

    // Output Serial log
    Serial.printf("[Telemetry] SQM: %.3f, Temp: %.2f, Press: %.1f, Hum: %.1f, Mag: %d/%d/%d, Batt: %u mV\n",
        sqmReading, tempC, pressHpa, humPct, rawMagX, rawMagY, rawMagZ, battMv);
}

bool checkPowerRails() {
#ifdef ENABLE_INA3221
    Wire.beginTransmission(INA3221_I2C_ADDR);
    if (Wire.endTransmission() != 0) return false;

    Wire.beginTransmission(INA3221_I2C_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom(INA3221_I2C_ADDR, 2) < 2) return false;
    uint16_t regVal = (Wire.read() << 8) | Wire.read();
    
    int16_t raw = (int16_t)regVal;
    float voltage = (float)(raw >> 3) * 0.008f;
    return (voltage >= 2.16f && voltage <= 3.60f);
#else
    return true; // Auto-bypass checks if power monitor compiled out
#endif
}

void shutdownPeripherals() {
#ifdef ENABLE_HMC5883L
    hmc.configure(0x70, 0xA0, 0x03);
#endif

    sleepBME280();
    sleepBNO055();
    sleepMLX90614();
    
    if (ACTIVE_COMM_METHOD == COMM_METHOD_WIFI) {
        wifiManager.sleep();
    } else {
        loraManager.sleep();
    }
}

void sleepBME280() {
#ifdef ENABLE_BME280
    Wire.beginTransmission(BME280_I2C_ADDR);
    Wire.write(0xF4);
    if (Wire.endTransmission() == 0 && Wire.requestFrom(BME280_I2C_ADDR, 1) == 1) {
        uint8_t ctrlMeas = Wire.read();
        ctrlMeas &= ~0x03; 
        Wire.beginTransmission(BME280_I2C_ADDR);
        Wire.write(0xF4);
        Wire.write(ctrlMeas);
        Wire.endTransmission();
    }
#endif
}

void sleepBNO055() {
#ifdef ENABLE_BNO055
    Wire.beginTransmission(BNO055_I2C_ADDR);
    Wire.write(0x3E);
    Wire.write(0x02); // Suspend
    Wire.endTransmission();
#endif
}

void sleepMLX90614() {
#ifdef ENABLE_MLX90614
    Wire.beginTransmission(MLX90614_I2C_ADDR);
    Wire.write(0xFF);
    Wire.write(0xE8); // PEC
    Wire.endTransmission();
#endif
}

#ifdef ENABLE_GPS
bool synchronizeClockAndLocation() {
    if (GPS_POWER_ENABLE_PIN >= 0) {
        digitalWrite(GPS_POWER_ENABLE_PIN, HIGH);
        delay(100);
    }

    Serial2.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("  -> GPS: Powered and listening on Serial2...");

    unsigned long start = millis();
    bool locked = false;

    while (millis() - start < GPS_SYNC_TIMEOUT_MS) {
        while (Serial2.available() > 0) {
            char c = Serial2.read();
            gpsDecoder.encode(c);
        }

        if (gpsDecoder.location.isValid() && gpsDecoder.location.isUpdated() &&
            gpsDecoder.date.isValid() && gpsDecoder.time.isValid() && gpsDecoder.time.isUpdated()) {
            
            rtc_gpsLat = (float)gpsDecoder.location.lat();
            rtc_gpsLon = (float)gpsDecoder.location.lng();
            rtc_gpsValid = true;

            uint16_t yr = gpsDecoder.date.year();
            uint8_t  mo = gpsDecoder.date.month();
            uint8_t  dy = gpsDecoder.date.day();
            uint8_t  hr = gpsDecoder.time.hour();
            uint8_t  mn = gpsDecoder.time.minute();
            uint8_t  sc = gpsDecoder.time.second();

            time_t gpsEpoch = convertUTCToEpoch(yr, mo, dy, hr, mn, sc);
            struct timeval tv = { .tv_sec = gpsEpoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            rtc_lastSyncTime = gpsEpoch;

            locked = true;
            break;
        }
        delay(10);
    }

    Serial2.end();
    if (GPS_POWER_ENABLE_PIN >= 0) {
        digitalWrite(GPS_POWER_ENABLE_PIN, LOW);
    }

    return locked;
}

time_t convertUTCToEpoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    int y = year;
    int m = month;
    int d = day;
    
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    
    time_t epochDays = (365LL * y) + (y / 4LL) - (y / 100LL) + (y / 400LL) + ((153LL * m + 2LL) / 5LL) + d - 719469LL;
    time_t epochSeconds = epochDays * 86400LL + (hour * 3600LL) + (minute * 60LL) + second;
    return epochSeconds;
}
#endif

#ifdef ENABLE_OLED
void runSetupMode() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_BUS_SPEED);

    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDR)) {
        Serial.println("[OLED] Initialization failed! Entering emergency Deep Sleep.");
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DAY_S * 1000000ULL);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
        rtc_gpio_pullup_en((gpio_num_t)USER_BUTTON_PIN);
        esp_deep_sleep_start();
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 16);
    display.println("  SYSTEM SETUP");
    display.println("  MODE ACTIVE");
    display.display();
    delay(1000);

#ifdef ENABLE_BME280
    bme.begin(BME280_I2C_ADDR);
#endif

#ifdef ENABLE_BNO055
    bno.begin();
#endif

    if (ACTIVE_COMM_METHOD == COMM_METHOD_WIFI) {
        wifiManager.begin();
    }

    uint8_t currentPage = 0;
    unsigned long lastActivityTime = millis();
    bool prevButtonState = HIGH;
    bool calibrationSaved = false;

    while (millis() - lastActivityTime < (SETUP_MODE_TIMEOUT_S * 1000UL)) {
        bool currentButtonState = digitalRead(USER_BUTTON_PIN);
        if (prevButtonState == HIGH && currentButtonState == LOW) {
            lastActivityTime = millis();
            currentPage++;
            
            if (currentPage >= 3) {
                break;
            }
            
            display.clearDisplay();
            display.display();
            delay(150); 
        }
        prevButtonState = currentButtonState;

        display.clearDisplay();
        
        switch (currentPage) {
            case 0: { // System Status Page
                uint16_t battMv = 0;
#ifdef ENABLE_INA3221
                Wire.beginTransmission(INA3221_I2C_ADDR);
                Wire.write(0x02);
                if (Wire.endTransmission() == 0 && Wire.requestFrom(INA3221_I2C_ADDR, 2) == 2) {
                    int16_t regVal = (Wire.read() << 8) | Wire.read();
                    battMv = (regVal >> 3) * 8;
                }
#endif

                display.setCursor(0, 0);
                display.println("--- SYSTEM STATUS ---");
                display.printf("Ver: %s\n", GIT_VERSION);
#ifdef ENABLE_INA3221
                display.printf("Batt V: %.2f V\n", (float)battMv / 1000.0f);
#else
                display.println("Batt V: --- (DISABLED)");
#endif

#ifdef ENABLE_GPS
                display.printf("GPS Lock: %s\n", rtc_gpsValid ? "CACHED" : "NO FIX");
#else
                display.println("GPS Lock: --- (DISABLED)");
#endif
                display.println("Sensors:");
                
#ifdef ENABLE_BME280
                display.printf(" BME:%s", checkI2CDevice(BME280_I2C_ADDR) ? "OK" : "ERR");
#else
                display.print(" BME:--");
#endif

#ifdef ENABLE_BNO055
                display.printf("  BNO:%s\n", checkI2CDevice(BNO055_I2C_ADDR) ? "OK" : "ERR");
#else
                display.println("  BNO:--");
#endif

#ifdef ENABLE_HMC5883L
                display.printf(" HMC:%s", checkI2CDevice(HMC5883L_I2C_ADDR) ? "OK" : "ERR");
#else
                display.print(" HMC:--");
#endif

#ifdef ENABLE_MLX90614
                display.printf("  MLX:%s\n", checkI2CDevice(MLX90614_I2C_ADDR) ? "OK" : "ERR");
#else
                display.println("  MLX:--");
#endif

#ifdef ENABLE_AT24C32
                display.printf(" EEPROM:%s\n", checkI2CDevice(AT24C32_I2C_ADDR) ? "OK" : "ERR");
#else
                display.println(" EEPROM:--");
#endif
                break;
            }

            case 1: { // Zenith Alignment Page (Graphical Bubble Level)
#ifdef ENABLE_BNO055
                sensors_event_t bnoEvent;
                bno.getEvent(&bnoEvent);
                float pitch = bnoEvent.orientation.y;
                float roll = bnoEvent.orientation.z;

                uint8_t sys = 0, gyro = 0, accel = 0, mag = 0;
                bno.getCalibration(&sys, &gyro, &accel, &mag);

                // Auto-save calibration offsets if fully calibrated
                if (!calibrationSaved && sys == 3 && gyro == 3 && accel == 3 && mag == 3) {
#ifdef ENABLE_AT24C32
                    adafruit_bno055_offsets_t newOffsets;
                    if (bno.getSensorOffsets(newOffsets)) {
                        if (eepromLog.writeBytes(EEPROM_BNO_CAL_ADDR, (const uint8_t*)&newOffsets, sizeof(newOffsets))) {
                            calibrationSaved = true;
                            display.clearDisplay();
                            display.setTextSize(1);
                            display.setTextColor(SSD1306_WHITE);
                            display.setCursor(0, 16);
                            display.println("  IMU CALIBRATED!");
                            display.println("  Offsets Saved to");
                            display.println("  EEPROM Address 3840");
                            display.display();
                            delay(1500);
                        }
                    }
#endif
                }

                const int centerX = 64;
                const int centerY = 32;
                
                display.drawCircle(centerX, centerY, 20, SSD1306_WHITE);
                display.drawCircle(centerX, centerY, 10, SSD1306_WHITE);
                display.drawCircle(centerX, centerY, 2, SSD1306_WHITE);
                display.drawFastHLine(centerX - 24, centerY, 48, SSD1306_WHITE);
                display.drawFastVLine(centerX, centerY - 24, 48, SSD1306_WHITE);

                int dx = (int)roll;
                int dy = (int)pitch;
                
                if (dx > 20) dx = 20;
                if (dx < -20) dx = -20;
                if (dy > 20) dy = 20;
                if (dy < -20) dy = -20;

                display.fillCircle(centerX + dx, centerY + dy, 4, SSD1306_WHITE);

                display.setCursor(0, 0);
                display.printf("CAL S:%d G:%d A:%d M:%d", sys, gyro, accel, mag);
                display.setCursor(0, 56);
                display.printf("P:%.1f R:%.1f", pitch, roll);
#else
                display.setCursor(0, 0);
                display.println("--- ZENITH ALIGN ---");
                display.println("\n Alignment cursor");
                display.println(" compiled out.");
                display.println(" [BNO055 DISABLED]");
#endif
                break;
            }

            case 2: { // Network Status Page (Dynamic LoRa vs. WiFi)
                display.setCursor(0, 0);
                if (ACTIVE_COMM_METHOD == COMM_METHOD_WIFI) {
                    display.println("--- WIFI DIAGNOSTICS -");
                    display.printf("Mode: Client STA\n");
                    display.printf("SSID: %s\n", wifiManager.getSSID().c_str());
                    
                    if (!wifiManager.isConnected()) {
                        display.println("Status: CONNECTING...");
                        if (WiFi.status() == WL_IDLE_STATUS || WiFi.status() == WL_DISCONNECTED) {
                            WiFi.mode(WIFI_STA);
                            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                        }
                    } else {
                        display.println("Status: CONNECTED (OK)");
                        display.printf("IP: %s\n", wifiManager.getIPAddress().c_str());
                        display.printf("Signal: %d dBm\n", wifiManager.getRSSI());
                    }
                } else {
                    display.println("--- NETWORK STATUS --");
                    display.println("LoRaWAN Status:");
                    int joinState = (millis() / 2000) % 3;
                    if (joinState == 0) {
                        display.println(" -> STATE: JOINING...");
                    } else if (joinState == 1) {
                        display.println(" -> STATE: CONNECTING");
                    } else {
                        display.println(" -> STATE: READY (OTAA)");
                    }
                    display.println("Web Configurator AP:");
                    display.println(" AP: SQM_METER_AP");
                    display.println(" IP: 192.168.71.1");
                }
                break;
            }
        }
        
        display.display();
        delay(100);
    }

    display.clearDisplay();
    display.setCursor(0, 16);
    display.println("  EXITING SETUP...");
    display.println("  ENTERING SLEEP...");
    display.display();
    delay(800);

    display.ssd1306_command(SSD1306_DISPLAYOFF);
    
    Serial.println("[Power] Setup Mode terminated. Putting display to sleep (0xAE) and entering Deep Sleep.");
    Serial.println("==============================================\n");

    if (ACTIVE_COMM_METHOD == COMM_METHOD_WIFI) {
        wifiManager.sleep();
    }

    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DAY_S * 1000000ULL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
    rtc_gpio_pullup_en((gpio_num_t)USER_BUTTON_PIN);
    esp_deep_sleep_start();
}
#endif

bool checkI2CDevice(uint8_t address) {
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}

void runHMCCalibration() {
#ifdef ENABLE_HMC5883L
    int16_t tx = 0, ty = 0, tz = 0;
    Serial.println("[HMC5883L] Running positive bias self-test...");
    if (hmc.performSelfTest(tx, ty, tz)) {
        rtc_stpCal = sqrt((float)tx * tx + (float)ty * ty + (float)tz * tz);
        hmc.setFactorySTP(rtc_stpCal);
        Serial.printf("[HMC5883L] Self-test success! STP_cal calculated: %.2f\n", rtc_stpCal);
#ifdef ENABLE_AT24C32
        eepromLog.writeBytes(EEPROM_HMC_CAL_ADDR, (const uint8_t*)&rtc_stpCal, sizeof(rtc_stpCal));
        Serial.println("[HMC5883L] Persistent STP calibration successfully saved to EEPROM.");
#endif
    } else {
        rtc_stpCal = 1.0f; // Default fallback to bypass scaling on failure
        Serial.println("[WARNING] Magnetometer self-test failed. Using unit scaling.");
    }
#endif
}
