# ESP32 Sky Quality Meter (SQM) Station

A scientific-grade, self-sustaining IoT instrument designed to measure night sky brightness (MPSAS), meteorological conditions, and celestial cloud cover for long-term remote field deployments. 

## Key Features
* **Multi-Sensor Shared Bus**: Integrates TSL2591 (lux/MPSAS), BME280 (temp/humidity/pressure), MLX90614 (infrared cloud cover), BNO055 (zenith leveling), and HMC5883L (pointing heading) over a shared 100 kHz I2C bus.
* **Persistent Calibration**: Auto-saves BNO055 register offsets and HMC5883L factory positive-bias temperature drift drift compensation factors to an AT24C32 EEPROM for instant boot-time restoration.
* **Dual Transmitters**: Modular communication routines for both LoRaWAN (LMIC) unconfirmed OTAA packets and WiFi HTTP JSON POST.
* **Dynamic Day/Night deep-sleep**: Executes NOAA solar-position algorithms offline to switch sleep durations automatically between Day (5-minute sleep) and Night (3-minute sleep) cycles.
* **Continuous Eclipse Mode**: Features a physical toggle switch to bypass deep sleep entirely, running a high-speed, non-blocking 2-second measurement loop.
* **Power Path Management**: Integrates an MCP73871 charger and a single-cell Li-Po battery, monitored across three high-side INA3221 shunt monitor channels.
