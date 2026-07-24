# Project Brief: Smart All-DC Solar Water Purification Controller (ESP32-S)

This document serves as the master specification file for the software development of an intelligent, off-grid, All-DC (24V) solar-powered Reverse Osmosis (RO) water purification system.

**Status:** This file is the single source of truth for system behavior. Whenever a design decision or work routine changes, this document must be updated first and then used as the implementation baseline.

---

## 1. System Architecture & Hardware Specs
* **Microcontroller:** ESP32-S (or compatible ESP32-WROOM-32).
* **Power Subsystem:** All-DC 24V. All high-power components (pumps, UV, solenoids) run directly from the 700W Solar Panel during the day.
* **Battery Subsystem:** 24V LiFePO4 battery, used **ONLY** for powering the 24/7 ESP32 smart system and the 12V night environmental lighting. Battery State of Charge (SoC %) must still be measured/estimated by firmware and published to the Web App (mandatory Home display). Sensing method (e.g. shunt/INA or voltage-based estimate) to be finalized in the firmware implementation phase.
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

**Locked clarifications (approved):**
1. **Flush flow requires BOTH path open:** During flush, inlet/source flow AND drain must be open together so water actually flows past TDS1. Scenario A: Inlet open (Relay1 OFF) **and** Drain open (Relay2 ON). Scenario B: Raw pump ON (Relay1 ON) **and** Drain open (Relay2 ON).
2. **5-second flow verification:** TDS1 may be accepted as “high” only after water has been flowing for at least **5 continuous seconds**. Do not trip on a momentary spike with no flow.
3. **Scenario B motor interlock:** Whenever Relay 1 is ACTIVE (raw pump ON), Relay 3 (purification pump + UV) must be forced OFF. The two motors must never run at the same time.
4. **Event logging / timestamp:** Intake fault and flush events must be logged. Date/time on logs is **deferred** until a clock source (RTC/NTP) is added; when that exists, every log entry must include date and time.

* **Scenario A (Mains):**
  * Normal: Inlet open (Relay1 OFF), Drain closed (Relay2 OFF).
  * Read TDS1 while water is flowing. Only after **≥ 5 s flow**, if TDS1 > Limit → close Inlet (Relay1 ON). Log event. Wait 30 minutes.
  * Flush: open Inlet (Relay1 OFF) **and** open Drain (Relay2 ON) for $t$ seconds; measure TDS1 during flush flow.
  * If still dirty → close Inlet again (Relay1 ON), Drain closed, repeat from the 30-minute wait.
  * If clean → Inlet open (Relay1 OFF), Drain closed (Relay2 OFF), return to normal.
  * *Note:* $t$ is calculated from municipal inlet piping volume.

* **Scenario B (Raw pump + 40L pressure tank):**
  * Raw pump (Relay1) runs only if $V_{solar} > V_{pump\_start}$. If solar too low → keep Relay1 OFF and wait.
  * Normal pumping: Relay1 ON, Drain closed (Relay2 OFF). (**Relay3 must stay OFF** while Relay1 is ON.)
  * Read TDS1 while flowing. Only after **≥ 5 s flow**, if TDS1 > Limit → Relay1 OFF, open Drain (Relay2 ON) to empty the 40L tank. Log event. Wait 30 minutes.
  * Flush: Relay1 ON **with** Drain open (Relay2 ON) for $t$ seconds; measure TDS1 during flush.
  * If still dirty → Relay1 OFF, keep/open Drain as needed, repeat from the 30-minute wait.
  * If clean → close Drain (Relay2 OFF), leave/return Relay1 ON to refill tank (subject to solar start condition).
  * *Note:* $t$ is based on pipe volume from raw source to TDS sensor.

### B. Purification Routine (روال کاری دوم: تصفیه)
* **Start Conditions:** Starts if Float Switch is `Tank_Low` (< 80% level), Pressure Switch is active (> 2 bar), $V_{solar} > V_{start}$ (calculated from the isolated ADS1115), Relay 1 is INACTIVE (Scenario B only — same motor interlock as intake), and no system faults are active.
* **Action:** Activate Relay 3 to run the high-pressure RO pump and UV lamp concurrently.
* **Stop Conditions:** Stops instantly if Float Switch is `Tank_Full` (100%), Pressure Switch is inactive (< 2 bar), $V_{solar} < V_{stop}$, Relay 1 is ACTIVE (Scenario B only), or any system fault is triggered.
* **Interlock reminder:** In Scenario B, Relay1 ON always forces Relay3 OFF (two motors must not run together).

### C. Night Environmental Lighting (روال کاری سوم: روشنایی شبانه)
* **Logic:** ESP32 continuously monitors the isolated solar panel voltage ($V_{solar}$) through the ADS1115 ADC.
* **Action (Hysteresis & 3-Minute Debounce):**
  * To prevent relay flickering (chattering) during sunset and sunrise, a **3-minute software debounce timer** and **software hysteresis** are implemented.
  * **Turn ON Condition:** If $V_{solar} < 5.0\text{V}$ continuously for more than 3 minutes, activate Relay 4 to turn on the 12V DC environmental lamps.
  * **Turn OFF Condition:** If $V_{solar} > 12.0\text{V}$ continuously for more than 3 minutes, deactivate Relay 4 to turn off the lamps.

---

## 5. System Faults & Protections (مدیریت خطاهای سیستم)

**Logging rule:** Fault and protection events must be logged. Timestamps (date/time) are added when the system clock (RTC/NTP) is implemented; until then logs may omit wall-clock time but must still record event type and key values.

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
* **UI Irradiance Mapping:** Measured $V_{solar}$ must be converted by firmware (or agreed shared formula) into an **irradiance percentage (0–100%)** for display in the Web App «میزان تابش» field. Control logic continues to use raw $V_{solar}$ thresholds; the percentage is a user-facing derived value.

---

## 7. Locked UI Decisions — Home Page (نهایی‌شده)
These decisions are approved and must be followed by the Web App. Firmware must expose the required live values when available.

### Display that must remain
* **Battery SoC (%):** Must always be shown on the Home header chip. Battery sensing/SoC algorithm is a firmware deliverable; until live data exists the UI may show `--`, but the battery chip must not be removed.
* **Salt rejection rate (%):** Remains on Home as a primary performance metric: `(1 - TDS_outlet / TDS_inlet) * 100`.
* **UV runtime hours:** Remains on Home; stays placeholder until Relay 3 runtime counter is live.
* **Irradiance (%):** Remains on Home; must be computed from $V_{solar}$ (see Section 6). Do not replace this field with raw volts.
* **Produced volume (L):** Remains; source of truth when live = `Relay_3_Runtime * Average_Pump_Flow`.
* **TDS dual rings (inlet/outlet):** Unchanged.
* **Temperature chip (Home header):** Temporarily continues to show product-water temperature from TDS Channel 2. When an ambient temperature sensor is added later, this chip switches to ambient temperature.

### Scenario-aware schematic
* Show active mode label next to the schematic title: Scenario A (mains) or Scenario B (raw pump).
* **Scenario A path:** Inlet valve → pre-filter → purification+UV → RO membrane → product tank (+ Drain path). No raw-water tank block.
* **Scenario B path:** Raw pump → 40L pressure tank → pre-filter → purification+UV → RO membrane → product tank (+ Drain path).
* Purification actuator label is **«تصفیه + UV»** (single Relay 3 actuator).
* **Product tank level:** binary only — Full / Low (Float Switch). Not a continuous percentage.
* **Scenario B raw tank display (inferred, not a level sensor):**
  * Raw pump ON → show tank level as near-empty.
  * Raw pump OFF → show tank level as full.
* Schematic footnote: product tank is Full/Low; maintenance detail lives on the Performance page.

### Home bottom status area (two separate boxes)
1. **Active routine box:** shows the current work routine (intake / purification / night lighting / wait states, etc.).
2. **Alerts box:** reserved for system warnings/faults ranked by importance (content filled when fault list is wired). Not a static “all OK” line mixed with routine text.

### Explicitly deferred on Home (do not add yet)
* Pressure switch status, leak status, and system-lock summary chips on Home (beyond what appears later inside the alerts box when faults are defined).

---

## 8. Firmware Implementation Roadmap (نقشه راه فیرمور)

### Phase 1 — Digital inputs & core routines (current)
Wired and active:
* Pressure Switch GPIO 18
* Product-tank Float Switch GPIO 27
* Leak Sensor GPIO 14 (**digital Active Low for this phase**)
* Relay map per Section 2 (new mapping)
* Scenario A/B stored in NVS; first boot asks user to select
* Purification routine using Float + Pressure + fault state
* Leak fault: full Section 5.1 action including Relay 1 by scenario
* Dry-run fault: Section 5.2
* **Temporary:** Purification ignores $V_{solar}$ start/stop until Phase 2. **Must be re-enabled when panel voltage is connected via ADS1115.**
* **Master system switch (Web Settings):** Single ON/OFF key for the whole plant (`cmd: power` / `systemEnabled`). OFF = safe shutdown (purify/night/drain off; Scenario A closes inlet; Scenario B stops raw pump). Leak lock still overrides. ON resumes automatic routines.

### Phase 2 — Analog via ADS1115
* Isolated $V_{solar}$ (irradiance % for UI + control feedback for purification/intake/night light)
* Battery SoC sensing/estimate for UI
* Leak sensor migrates from digital GPIO 14 to analog channel (threshold-based)

### Phase 3 — Capability review
* Verify all wired hardware features are used correctly
* Identify additional functions possible with the same hardware set
