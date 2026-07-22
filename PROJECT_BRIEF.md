# Project Brief: Smart All-DC Solar Water Purification Controller (ESP32-S)

This document serves as the master specification file for the software development of an intelligent, off-grid, All-DC (24V) solar-powered Reverse Osmosis (RO) water purification system.

**Status:** This file is the single source of truth for system behavior. Whenever a design decision or work routine changes, this document must be updated first and then used as the implementation baseline.

---

## 1. System Architecture & Hardware Specs
* **Microcontroller:** ESP32-S (or compatible ESP32-WROOM-32).
* **Power Subsystem:** All-DC 24V. All high-power components (pumps, UV, solenoids) run directly from the 700W Solar Panel during the day.
* **Battery Subsystem:** 24V LiFePO4 battery, used **ONLY** for powering the 24/7 ESP32 smart system and the 12V night environmental lighting.
* **Sensor Bus:** 100% Digital / Isolated I2C / UART topology. The solar voltage sensing is electrically isolated from the main board using the **ISO1540** bidirectional I2C digital isolator and the **ADS1115** 16-bit ADC. No direct analog connection to the solar panel exists on the ESP32 side.
* **Sensor Power Gating:** P-Channel MOSFET controlled by ESP32 to cut VCC to the TDS module and Pressure Switch during Deep Sleep or idle states.
* **Relay Output Channels:** Exactly 4x Mechanical Relays.

---

## 2. ESP32 Pin Mapping (GPIO Connections)

| Device / Module | Signal Type | ESP32 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **Relay 1** | Digital Output | GPIO 13 | NO Contact. Scenario A: Inlet Solenoid / Scenario B: Raw DC Pump |
| **Relay 2** | Digital Output | GPIO 15 | NC Contact. Drain Solenoid Valve (both scenarios) |
| **Relay 3** | Digital Output | GPIO 2 | NO Contact. Purification Pump (24V) + UV Lamp (24V) |
| **Relay 4** | Digital Output | GPIO 12 | NO Contact. Night Environmental Lighting (12V DC Lamps) |
| **P-MOSFET Switch** | Digital Output | GPIO 4 | Active Low. Gates VCC to TDS Module and Pressure Switch |
| **TDS Module (UART)** | UART Serial | RX2 (GPIO 16) / TX2 (GPIO 17) | Dual-channel TDS + Temperature module |
| **ISO1540 + ADS1115 (I2C)** | I2C Serial | SDA (GPIO 21) / SCL (GPIO 22) | Isolated I2C bus connected to the 16-bit ADS1115 ADC |
| **Pressure Switch** | Digital Input | GPIO 18 | High = Pressure > 2 bar / Low = Pressure < 2 bar |
| **Leak Sensor** | Digital Input | GPIO 14 | Interrupt Pin. Active Low (Water detected) |
| **Float Switch** | Digital Input | GPIO 27 | High = Tank Full (100%) / Low = Tank Low (< 80%) (Requires Pull-up) |

---

## 3. System Modes & Boot Initialization (روال پیکربندی)
On the very first boot after uploading firmware, the ESP32 must check the Non-Volatile Storage (NVS/EEPROM).
* If `System_Mode` is not found, it boots into a temporary AP mode/Serial config interface and asks the user to select:
  * **Scenario A:** Mains/Tap water.
  * **Scenario B:** Raw water pump + 40L pressure tank.
* The selected mode (`Scenario_A` or `Scenario_B`) is saved in NVS, and the board restarts.

---

## 4. System Work Routines (روال‌های کاری سیستم)

### A. Water Intake Routine (روال کاری اول: آب‌گیری)
* **Scenario A (Mains):** Inlet valve (Relay 1, NO) is normally open. If TDS Channel 1 > Limit (after 5s flow verification), close Inlet valve. Wait 30m. Open Inlet and Drain (Relay 2, NC) for $t$ seconds (flushing pipe). Measure TDS. If clean, resume. If dirty, close inlet and repeat 30m wait.
  * *Note:* $t$ is calculated based on municipal inlet piping volume.
* **Scenario B (Pump):** Raw pump (Relay 1, NO) runs only if the isolated solar voltage $V_{solar} > V_{pump\_start}$. If TDS Channel 1 > Limit, turn off Raw pump, open Drain valve (Relay 2, NC) to drain the 40L tank. Wait 30m. Run pump with open drain for $t$ seconds to flush. Measure TDS. If clean, close drain and refill tank. If dirty, stop pump and repeat 30m wait.
  * *Note:* $t$ is based on pipe volume from raw source to TDS sensor.
  * **Interlocking Rule:** In Scenario B, whenever Relay 1 is ACTIVE, Relay 3 (Purification) must be forced INACTIVE (OFF) to prevent concurrent power surges on the solar bus.

### B. Purification Routine (روال کاری دوم: تصفیه)
* **Start Conditions:** Starts if Float Switch is `Tank_Low` (< 80% level), Pressure Switch is active (> 2 bar), $V_{solar} > V_{start}$ (calculated from the isolated ADS1115), Relay 1 is INACTIVE (Scenario B only), and no system faults are active.
* **Action:** Activate Relay 3 to run the high-pressure RO pump and UV lamp concurrently.
* **Stop Conditions:** Stops instantly if Float Switch is `Tank_Full` (100%), Pressure Switch is inactive (< 2 bar), $V_{solar} < V_{stop}$, Relay 1 is ACTIVE (Scenario B only), or any system fault is triggered.

### C. Night Environmental Lighting (روال کاری سوم: روشنایی شبانه)
* **Logic:** ESP32 continuously monitors the isolated solar panel voltage ($V_{solar}$) through the ADS1115 ADC.
* **Action (Hysteresis & 3-Minute Debounce):**
  * To prevent relay flickering (chattering) during sunset and sunrise, a **3-minute software debounce timer** and **software hysteresis** are implemented.
  * **Turn ON Condition:** If $V_{solar} < 5.0\text{V}$ continuously for more than 3 minutes, activate Relay 4 to turn on the 12V DC environmental lamps.
  * **Turn OFF Condition:** If $V_{solar} > 12.0\text{V}$ continuously for more than 3 minutes, deactivate Relay 4 to turn off the lamps.

---

## 5. System Faults & Protections (مدیریت خطاهای سیستم)

### 1. Water Leakage Fault
* **Trigger:** GPIO 14 pulled LOW.
* **Action:** Turn off Relay 3. Scenario A: Turn on Relay 1 (Close Inlet NO). Scenario B: Turn off Relay 1 (Stop Pump).
* **State:** Lock system permanently until physical reset.

### 2. Inlet Low Pressure / Dry-Run Fault
* **Trigger:** Relay 3 is active, but Pressure Switch remains open (pressure < 2 bar) for > 30 consecutive seconds.
* **Action:** Turn off Relay 3 and stop water intake. Wait 15m. Retry up to 3 times.
* **State:** If it fails 3 times consecutively, lock the system permanently.

### 3. UV Lamp Replacement Fault
* **Tracking:** Accumulate Relay 3 run-time. Write total hours to NVS every 1 hour.
* **Trigger:** Total runtime > UV_Life_Threshold (e.g., 9000 hours).
* **State:** Turn off all relays and lock system until lamp replacement and manual reset.

### 4. Pre-Filter Replacement Fault
* **Tracking:** Estimate volume: `Volume = Relay_3_Runtime * Average_Pump_Flow`. Write to NVS every 1 hour.
* **Trigger:** Total volume > Pre_Filter_Volume_Limit (e.g., 5000 liters).
* **State:** Stop all operations and lock system until pre-filters are replaced and volume is reset.

### 5. RO Membrane Degradation Fault
* **Trigger:** Purified Water TDS (TDS Channel 2) > Danger_Limit.
* **Logic:** Start long-term verification. Every 100 liters produced, take a 5-second TDS average. Repeat 5 times (total 500 liters).
  * If **all 5** readings > Danger_Limit: Log error, stop all relays, and lock system until RO membrane replacement.
  * If **at least one** reading < Danger_Limit: Cancel long-term test, clear warning, return to normal.

---

## 6. Isolated Solar Voltage & Irradiance Sensing Subsystem
To ensure electrical safety and isolate the low-voltage microcontroller circuit from high-voltage transients on the solar panel:
* **Analog Front-End (AFE):** A high-impedance voltage divider composed of $200\text{ k}\Omega$ (high-side) and $10\text{ k}\Omega$ (low-side) resistors must scale the maximum open-circuit solar panel voltage ($V_{oc}$ up to $60\text{V}$) down to a safe analog range (under $3.3\text{V}$).
* **Over-Voltage & Transient Protection:** A $5.0\text{V}$ Transient Voltage Suppressor (TVS) diode (or a $3.3\text{V}$ Zener diode) must be connected in parallel with the $10\text{ k}\Omega$ low-side resistor to clamp any voltage spikes and protect the ADS1115 analog input pin (A0). A $100\text{nF}$ ceramic capacitor is connected in parallel as a low-pass noise filter.
* **Galvanic Isolation:** The I2C bus (SDA and SCL) must be routed through the **ISO1540** bidirectional digital isolator. The isolated side of the ISO1540 and the ADS1115 ADC must be powered by an isolated $5\text{V}/3.3\text{V}$ power source derived from the solar side, keeping the ESP32 system ground completely isolated from the solar panel ground ($PV-$).
* **PCB Layout Constraints:** A physical creepage and clearance distance of **at least 4 mm** must be maintained on the PCB layout between the isolated PV ground copper planes/traces and the ESP32 system ground planes/traces. No copper or components must cross this isolation barrier except for the ISO1540 chip itself.
