# Hardware Assembly & Setup Roadmap: ESP32 Sky Quality Meter (SQM)

This document provides the step-by-step mechanical assembly roadmap, electrical connection schematics, physical sensor calibration procedures, and testing guidelines to physically build and deploy the ESP32 Sky Quality Meter (SQM) Station.

---

## Roadmap Overview

```text
 PHASE 1: BOM & ESD Prep  ──>  PHASE 2: I2C & UART Wiring  ──>  PHASE 3: MOSFET GPS Switch
                                                                          │
                                                                          ▼
 PHASE 6: Field Deployment ──<  PHASE 5.5: IMU Calibration ──<  PHASE 5: Setup & Leveling
```

---

## Phase 1: Bill of Materials (BOM) & Tools

Prior to assembly, ensure you have the following modules and tools. Operate in an **ESD-safe environment** (anti-static wrist strap, grounded mat) to prevent static damage to the sensitive CMOS sensors.

### Component List
1. **Core Microcontroller**: ESP32-WROOM-32D DevKitC Board.
2. **Sky Brightness Sensor**: Adafruit TSL2591 High-Dynamic Digital Light Sensor.
3. **Environment Sensor**: Adafruit BME280 Temperature, Humidity, and Barometric Pressure Sensor.
4. **Celestial Cloud Sensor**: Melexis MLX90614 Non-Contact Infrared Sky Temperature Sensor.
5. **Orientation & Tilt Sensor**: Adafruit BNO055 9-DOF Absolute Orientation IMU.
6. **Magnetometer (Pointing Heading)**: Honeywell HMC5883L 3-Axis Digital Compass.
7. **Battery Monitor**: TI INA3221 Triple-Channel Shunt Monitor Board (monitors USB/Solar input, Battery, and active System load).
8. **Logging Storage**: AT24C32 I2C 32Kbit EEPROM Board.
9. **Location & Clock Sync**: NEO-M8 UART GPS Receiver Module.
10. **Local Display**: SSD1306 128x64 I2C OLED Display.
11. **Power Management & Charging**: MCP73871 USB/Solar Lithium-Ion/Polymer Charger and Power Path Management IC.
12. **Energy Storage**: 3.7V Single-Cell Lithium-Polymer (Li-Po) Battery (e.g., 2000mAh or larger, JST-PH connector).
13. **Inputs**:
    * 1x Momentary Tactile Push Button (USER Setup Mode).
    * 1x SPST Toggle Switch (Eclipse Mode Select).
14. **Power Gate Component**: 1x P-Channel MOSFET (e.g., NDP6020P or equivalent) and 1x 10kΩ resistor (for GPS power cutoff circuit).
15. **Passive Components**: 2x 2.2kΩ Resistors (I2C SDA/SCL pull-ups).
16. **Enclosure**: Weatherproof IP67 Junction Box with a transparent polycarbonate optical dome.

---

## Phase 2: Electrical Schematics & Connection Diagrams

The system utilizes a shared I2C bus at 100 kHz for 7 sensor/display blocks, a dedicated high-speed UART connection for the GPS module, and digital input lines for the button and toggle switches.

### 1. Shared I2C Bus Wiring (SDA/SCL)

> [!IMPORTANT]
> All 7 I2C devices must share the exact same I2C SDA (GPIO 21) and SCL (GPIO 22) lines. Connect **physical 2.2kΩ pull-up resistors** from SDA to 3.3V and from SCL to 3.3V. Do not rely on internal ESP32 pull-up resistors, which are too weak (typically 10kΩ-47kΩ) and will cause bus rise-time failures.

```text
                    3.3V (VCC)
                      │
                     [2.2k] Pull-up
                      │
SDA (GPIO 21) ────────┴────────────────────────── shared SDA bus across 7 devices
                      │
                     [2.2k] Pull-up
                      │
SCL (GPIO 22) ────────┴────────────────────────── shared SCL bus across 7 devices
```

#### Point-to-Point I2C Mapping
* **ESP32 3.3V Out** $\rightarrow$ Connected to VCC pins of: BME280, TSL2591, MLX90614, BNO055, HMC5883L, AT24C32, INA3221, and SSD1306.
* **ESP32 GND** $\rightarrow$ Common ground connected to GND pins of all 8 devices.
* **ESP32 GPIO 21 (SDA)** $\rightarrow$ Connected to SDA / SDIO pin of all I2C modules.
* **ESP32 GPIO 22 (SCL)** $\rightarrow$ Connected to SCL / SCK pin of all I2C modules.

### 2. Physical Button & Toggle Switch Connections
* **USER Button**: Connect one terminal to **ESP32 GPIO 12** and the other terminal to **GND**.
* **Eclipse SPST Switch**: Connect one terminal to **ESP32 GPIO 13** and the other terminal to **GND**.

*Note: Internal pullups are enabled in the C++ code (`INPUT_PULLUP`), so no external resistors are required for the switches.*

### 3. MCP73871 Power Path & INA3221 Multi-Channel Wiring

To achieve safe battery charging, uninterruptible system operation, and complete real-time power analytics, the system utilizes an **MCP73871 Power Path Management IC** and a single-cell **3.7V Lithium-Polymer Battery**. The **INA3221 Triple-Channel Shunt Monitor** is wired inline to measure input power, battery charging/discharging, and active system load separately.

#### High-Level Power Routing Schematic

```text
       Incoming Power (USB 5V / Solar 5V-6V)
                     │
                     ▼
             ┌──────────────┐
             │  CH2 IN+     │
             │              │  (INA3221 Channel 2 - Input Power Monitor)
             │  CH2 IN-     │
             └───────┬──────┘
                     │
                     ▼
             ┌──────────────┐
             │    IN / V+   │
             │              │
             │   MCP73871   │
             │     PMIC     │
             │              │
             │     BAT      │       LOAD / OUT
             └───────┬──────┴───────┬──────────┘
                     │              │
                     ▼              ▼
             ┌──────────────┐┌──────────────┐
             │  CH1 IN+     ││  CH3 IN+     │
             │              ││              │  (INA3221 Channel 3 - System Consumption)
             │  CH1 IN-     ││  CH3 IN-     │
             └───────┬──────┘└───────┬──────┘
                     │              │
                     ▼              ▼
             ┌──────────────┐┌──────────────┐
             │ Li-Po Battery││3.3V Regulator│ ──> ESP32 and Sensor VCC Bus (3.3V)
             │   (3.7V)     ││  (LDO Input) │
             └──────────────┘└──────────────┘
```

#### Detailed Point-to-Point Electrical Connections

| Source Module/Pin | Target Module/Pin | Wire Function |
| :--- | :--- | :--- |
| **Solar Panel (+) / USB 5V** | INA3221 `CH2 IN+` | High-side power input line |
| **INA3221 `CH2 IN-`** | MCP73871 `IN` (V+) | Monitored supply to the battery charger module |
| **MCP73871 `BAT`** | INA3221 `CH1 IN+` | High-side battery charger output |
| **INA3221 `CH1 IN-`** | Li-Po Battery (+) Terminal | Monitored battery charging / discharging current line |
| **Li-Po Battery (-)** | Common GND | Battery reference ground |
| **MCP73871 `LOAD` (OUT)** | INA3221 `CH3 IN+` | High-side system supply from PMIC |
| **INA3221 `CH3 IN-`** | 3.3V LDO Regulator `IN` pin | Monitored input supply to the 3.3V voltage regulator |
| **LDO Regulator `OUT`** | ESP32 `3V3` & I2C VCC bus | Clean regulated 3.3V system power line |
| **All Module GND Pins** | Common GND | Common system ground rail |

> [!IMPORTANT]
> **Why Channel 1 is Battery**:
> The ESP32 firmware reads the battery voltage from the Channel 1 Bus Voltage Register (`0x02`). Wiring the battery positive terminal downstream of Channel 1 (`CH1 IN-`) ensures that the current battery reading code works perfectly without needing configuration modifications.
>
> **Smart Load Sharing**:
> The MCP73871 automatically powers the system directly from the Solar Panel / USB when present, while concurrently charging the battery. If the charging source is disconnected, it immediately switches the system output to battery power with **zero switchover lag**, avoiding processor resets.

---

## Phase 3: MOSFET GPS Power Gating Circuit

To prevent the NEO-M8 GPS module from drawing 15-50mA during deep sleep, we gate its power rail using a P-channel MOSFET controlled by the ESP32.

```text
               Source      Drain
 3.3V Rail ─────[ P-MOSFET ]───────────> GPS VCC In
                  │ Gate
                  ├──────[ 10k Resistor ]─────── 3.3V Rail (Keeps PMOS OFF by default)
                  │
  ESP32 GPIO 17 ──┴── (Drive LOW to turn GPS ON, HIGH/Floating to turn GPS OFF)
```

### Wiring the Power-Gate
1. Connect the **MOSFET Source** to the ESP32 3.3V power rail.
2. Connect the **MOSFET Drain** to the NEO-M8 GPS `VCC` input pin.
3. Connect a **10kΩ Pull-up Resistor** between the MOSFET Gate and the 3.3V power rail. This ensures the MOSFET remains completely turned off (GPS powered down) when the ESP32 is asleep or the GPIO pin is in a high-impedance state.
4. Connect the **MOSFET Gate** directly to **ESP32 GPIO 17** (GPS power control pin).
5. Connect **NEO-M8 TX** $\rightarrow$ **ESP32 GPIO 16** (Serial2 RX).
6. Connect **NEO-M8 RX** $\rightarrow$ **ESP32 GPIO 17** (Serial2 TX - shared control or direct).
7. Connect **NEO-M8 GND** $\rightarrow$ Common Ground.

---

## Phase 4: Weatherproof Mechanical Enclosure Assembly

A high-precision optical instrument must be mechanically isolated from precipitation while maintaining a clear view of the night sky.

```text
          ┌───────────────────────────────────┐
          │  Transparent Optical Dome (Glass) │
          └─────────────────┬─────────────────┘
                            │
               ┌────────────┴────────────┐
               │ TSL2591 Light Sensor    │ <── Upward Zenith Pointing
               │ MLX90614 IR Sky Temp    │
               └────────────┬────────────┘
                            │
               ┌────────────┴────────────┐
               │ BNO055 Flat Mount Plate │ <── Aligned parallel to sensor window
               │ HMC5883L & Core ESP32   │
               └────────────┬────────────┘
                            │
          ┌─────────────────┴─────────────────┐
          │     Weatherproof IP67 Box         │
          └───────────────────────────────────┘
```

1. **Optical Window Placement**:
   * Mount the upward-pointing sensors (**TSL2591** and **MLX90614**) directly underneath the transparent polycarbonate/glass dome at the top of the box.
   * **TSL2591 Position**: Ensure the light sensor's photodiode is perfectly centered under the glass, with no structural obstacles blocking its $180^\circ$ hemisphere view.
   * **MLX90614 Position**: Mount the infrared sensor canister close to the transparent window. Note that standard glass/polycarbonate blocks far-infrared spectra (used for cloud temperature). For professional cloud detection, the window over the MLX90614 must be cut and covered with an **infrared-transparent silicon window** or a high-density polyethylene (HDPE) film, sealed with marine-grade silicone.
2. **Sensor Plate Mounting**:
   * Mount the **BNO055** flat on the internal structural plate. The sensor must be parallel to the upward-pointing TSL2591 window so that its Pitch and Roll values accurately represent the light sensor's tilt.
   * Mount the **HMC5883L** at least **5 cm (2 inches)** away from the ESP32, power supply, batteries, or metal mounting screws. Magnetic fields from high-current tracks or iron screws will distort magnetometer calibration.
3. **Cable Entry**:
   * Drill cable entry holes at the *bottom* of the enclosure. Install IP68 liquid-tight cable glands to route external antennas (LoRa/WiFi) or solar power input lines.

---

## Phase 5: Initial Setup & Field Zenith Leveling

Once mechanically assembled, execute the following setup sequence to align the SQM station on its mounting pole in the dark.

```text
Step 1: Hold USER Button & Power ON  ──>  OLED displays: SYSTEM STATUS Page
                                                     │
                                                     ▼
Step 2: Single-click Button           ──>  OLED displays: ZENITH ALIGN Page
                                                     │
                                                     ▼
Step 3: physically Level Mounting Pole ──>  Adjust until Cursor bubble centers
                                                     │
                                                     ▼
Step 4: Single-click Button           ──>  OLED displays: NETWORK STATUS Page
                                                     │
                                                     ▼
Step 5: Hold Button for 3 Seconds     ──>  OLED sleeps (0xAE), ESP32 enters Deep Sleep
```

### Digital Bubble Level Calibration
1. Power up the device into **Setup Mode** by holding down the USER button.
2. The OLED screen will turn on, showing the **SYSTEM STATUS** page. Verify all I2C sensors are detected and battery voltage is correct:
   * Displays: `BME:OK  BNO:OK  HMC:OK  MLX:OK  EEPROM:OK`.
3. Tap the USER button once to transition to Page 1: the **ZENITH ALIGN** bubble level page.
4. Mount the enclosure on its physical pole.
5. In the dark, watch the OLED screen. It draws a target concentric ring representing the zenith. The filled bubble cursor represents the sensor's real-world tilt.
6. Loosen the pole clamps and tilt the box until the bubble cursor lands directly in the center target ring, and Pitch/Roll metrics read close to **0.0 degrees**.
7. Tighten the pole clamps securely. Your sky brightness sensor is now aligned pointing exactly at the zenith.
8. Tap the USER button once more to check the **NETWORK DIAGNOSTICS** page. Verify your router connection signal (RSSI) is strong or LoRaWAN has connected.
9. Hold the button down for 3 seconds (or simply let the 3-minute idle timer expire). The display will turn off completely (`0xAE`), cutting its current draw to under 10 µA, and the ESP32 will enter standard Day/Night logging mode.

---

## Phase 5.5: Sensor Calibration Routines

To achieve maximum scientific precision, the three primary directional and optical sensors must be calibrated. The system incorporates physical movement calibration, automated electrical self-tests, and zero-point calibration offsets.

---

### 1. BNO055 9-DOF IMU Zenith Leveling Calibration
To achieve reliable $\pm0.1^\circ$ zenith leveling accuracy, the BNO055 absolute orientation sensor must be physically calibrated. The BNO055 runs an onboard DSP continuous calibration engine. The Setup Mode Alignment page (Page 1) displays live calibration status indices from `0` (completely uncalibrated) to `3` (fully calibrated):

* **S (System)**: Combined sensor fusion status.
* **G (Gyroscope)**: Rotation rate zero-point calibration.
* **A (Accelerometer)**: Gravity vector scaling.
* **M (Magnetometer)**: Earth's magnetic heading alignment.

#### Physical Calibration Movements
Perform these specific physical movements in the field to calibrate the sensor blocks:

```text
 1. GYROSCOPE (G)       ──>  Keep device flat, level, and completely STILL for 2-3 seconds.
                                                     │
                                                     ▼
 2. MAGNETOMETER (M)    ──>  Move enclosure slowly in an air-drawn "FIGURE-8" pattern 3-4 times.
                                                     │
                                                     ▼
 3. ACCELEROMETER (A)   ──>  Rotate board in 90-degree steps, resting on each face for 2s.
                                                     │
                                                     ▼
 4. PERSISTENT SAVE     ──>  All scores reach "3". Offsets AUTO-SAVE to AT24C32 EEPROM.
```

* **Gyroscope Calibration**: Keep the SQM station completely flat and motionless for **2 to 3 seconds**. The Gyro indicator `G` will quickly climb to `3`.
* **Magnetometer Calibration**: Pick up the enclosure and slowly wave it in a **3D Figure-8 pattern** in the air, rotating the unit along different axes. Repeat this 3 to 4 times until the Magnetometer indicator `M` climbs to `3`.
* **Accelerometer Calibration**: Rotate the device in 90-degree increments, pausing for 2 seconds on each perpendicular face (flat on base, left side, right side, upside-down). Once at least 3-4 perpendicular angles are registered, the Accelerometer indicator `A` will climb to `3`.
* **Automatic Offset Storage**: Once `S`, `G`, `A`, and `M` all reach `3` (fully calibrated), the OLED will display **`IMU CALIBRATED!`** and the firmware will query the 22-byte offset parameters from the BNO055 registers, saving them to **EEPROM Address 3840**.
* **Instant Restore on Boot**: On future wake cycles, the ESP32 automatically reads these 22 bytes from the EEPROM and writes them back into the BNO055 registers in under **1 millisecond**, restoring full calibration without requiring manual movement.

---

### 2. HMC5883L Magnetometer Temperature Drift Calibration
Magnetometers suffer from sensitivity drift caused by ambient temperature changes. To stabilize readings without adding an external thermometer or thermal housing, the system implements an automated self-test and positive bias calibration:

* **Triggering Self-Test**: On first boot (or when battery power is fully disconnected), the ESP32 applies an internal positive bias current (~10 mA) to the HMC5883L sensor coils.
* **Calculating STP Baseline**: The firmware triggers a measurement to read the raw magnetic field strength vector under positive bias. It computes the vector magnitude:
  $$STP_{cal} = \sqrt{X_{bias}^2 + Y_{bias}^2 + Z_{bias}^2}$$
* **EEPROM Persistence**: The calculated $STP_{cal}$ value is saved persistently to **EEPROM Address 3870**. On future wake-ups, this value is restored to the controller within milliseconds.
* **Sensitivity Temperature Compensation**: During standard measurement runs, the HMC5883L triggers a brief positive bias reading $STP_{curr}$. It scales all raw readings using the temperature-invariant ratio:
  $$X_{compensated} = X_{raw} \times \frac{STP_{cal}}{STP_{curr}}$$
  This eliminates temperature-induced gain variations dynamically in the field.

---

### 3. TSL2591 Sky Quality (MPSAS) Zero-Point Calibration
The sky quality readings are calculated from raw digital lux measurements using the astronomical magnitude formula:
$$MPSAS = ZP - 2.5 \log_{10}(\text{lux})$$

* **Calibration Zero Point ($ZP$)**: The zero-point constant defaults to `21.0` in the firmware config, representing the natural brightness Zero-Point calibration offset.
* **Field Cross-Calibration**: To calibrate your SQM station against an industry-standard reference instrument (such as a Unihedron SQM-L):
  1. Mount the SQM station and reference meter pointing at the same region of the sky on a clear, dark, moonless night.
  2. Record consecutive readings from the reference meter ($MPSAS_{ref}$) and the SQM station ($MPSAS_{station}$).
  3. Calculate the difference: $\Delta = MPSAS_{ref} - MPSAS_{station}$.
  4. Adjust the calibration Zero-Point in the firmware configuration file:
     ```cpp
     #define SQM_ZERO_POINT  (21.0f + delta)
     ```
  5. Compile and upload the updated configuration. This shifts the calibration scale to match the reference meter with scientific accuracy.

---

## Phase 6: Electrical & Signal Validation Testing

Before leaving the station unattended, perform these system checks using the serial console:

1. **Multi-Channel Power System Verification**:
   * Measure current on all three channels via the INA3221 and cross-verify with a high-precision inline multimeter:
     * **Channel 1 (Li-Po Battery Rail)**:
       * *Solar/USB Connected*: Shows positive battery charging current (up to 500-1000mA based on MCP73871 `PROG1` resistor configuration), gradually decreasing to 0mA when the battery reaches the 4.20V termination threshold.
       * *Solar/USB Disconnected*: Shows negative battery discharging current. During active sensing phases, current draw lies in the range of **40mA to 70mA** (up to 220mA during WiFi transmission bursts). During Deep Sleep, battery discharging current must drop below **50 µA**.
     * **Channel 2 (Solar/USB Input Rail)**:
       * *Solar/USB Connected*: Shows positive incoming current from the source (proportional to solar irradiation or USB supply capacity).
       * *Solar/USB Disconnected*: Must read exactly **0mA**. If leakage current is detected on the input rail, check for reverse-current diode faults in the solar panel cabling.
     * **Channel 3 (System Load Rail)**:
       * Shows the actual active current consumed by the LDO, ESP32, and sensors combined. This must remain **positive** at all times (Active: 40-70mA, Deep Sleep: < 50 µA), regardless of whether power is supplied from the battery or Solar/USB source.
       * *Note*: If the deep-sleep load current exceeds 100 µA, inspect for parasitic leakage on sensor pin states or I2C pullups connected to unswitched power lines.
2. **Periodic Sleep Duty Validation**:
   * Open the Serial terminal.
   * If in **Day Mode**, verify the system logs data and enters sleep for exactly 300 seconds (5 minutes).
   * Cover the TSL2591 completely to simulate night. Once the astronomical scheduler transitions the system, verify it logs data (including sky brightness) and enters deep sleep for exactly 180 seconds (3 minutes).
3. **Eclipse Switch Interrupt Check**:
   * Toggle the Eclipse Switch to Ground (`LOW`).
   * Verify the ESP32 wakes up instantly and outputs data to the Serial monitor **every 2 seconds**, and the OLED display updates in real-time.
   * Toggle the Eclipse Switch off (`HIGH`). Verify the system exits the continuous loop and goes back to deep sleep.
