# Project Brief: Smart All-DC Solar Water Purification Controller (ESP32-S)

This document serves as the master specification file for the software development of an intelligent, off-grid, All-DC (24V) solar-powered Reverse Osmosis (RO) water purification system.

**Status:** This file is the single source of truth for system behavior. Whenever a design decision or work routine changes, this document must be updated first and then used as the implementation baseline.

---

## 1. System Architecture & Hardware Specs
* **Microcontroller:** ESP32-S (or compatible ESP32-WROOM-32).
* **Power Subsystem:** All-DC 24V. High-power components (pumps, UV, solenoids) run directly from the 700W Solar Panel during the day.
* **Battery Subsystem:** 24V LiFePO4 battery bank, charged via the **EPEver Tracer BN MPPT Charge Controller**. Used **ONLY** for 24/7 ESP32 power and 12V night environmental lighting.
* **Energy & Solar Telemetry:** 100% digital Modbus RTU over an **Isolated RS485 Transceiver Module** (hardware auto-direction) connected to the Tracer BN. Reads $V_{solar}$, battery voltage, charging current, and battery SoC % digitally. **No** analog high-voltage dividers or external ADCs for solar/battery on the board.
* **Sensor Power Gating:** P-Channel MOSFET (GPIO 4, Active Low enable) cuts VCC to the TDS module and pressure sensors during Deep Sleep or idle sensor-off states.
* **Relay Output Channels:** Exactly 4× mechanical relays.

---

## 2. ESP32 Pin Mapping (GPIO Connections)

| Device / Module | Signal Type | ESP32 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **Relay 1** | Digital Output | GPIO 13 | NO. Scenario A: Inlet solenoid / Scenario B: Raw DC pump |
| **Relay 2** | Digital Output | GPIO 15 | NC coil sense in hardware. Drain solenoid (both scenarios) |
| **Relay 3** | Digital Output | GPIO 2 | NO. Purification pump (24V) + UV lamp (24V) |
| **Relay 4** | Digital Output | GPIO 12 | NO. Night environmental lighting (12V) |
| **P-MOSFET Switch** | Digital Output | GPIO 4 | Active Low. Gates VCC to TDS module & pressure sensors |
| **TDS Module (UART2)** | UART Serial | RX2 GPIO 16 / TX2 GPIO 17 | Dual-channel TDS + temperature |
| **Isolated RS485 (UART1)** | UART Serial | RX1 GPIO 25 / TX1 GPIO 26 | Modbus RTU ↔ Tracer BN. **No DE/RE GPIO** (auto-direction module) |
| **Pressure Transducer** | Analog Input | GPIO 35 | Scenario B 40L tank pressure (0.5–4.5 V via 10k/20k divider + RC) |
| **Pressure Switch** | Digital Input | GPIO 18 | Scenario A mains pressure (> ~2 bar when HIGH) |
| **Leak Sensor** | Digital Input | GPIO 14 | Interrupt. Active Low = water detected |
| **Float Switch** | Digital Input | GPIO 27 | HIGH = product tank full (100%) / LOW = low (< 80%). Pull-up required |

---

## 3. Boot Initialization (روال پیکربندی)
On first boot after firmware upload, ESP32 checks NVS:
* If `System_Mode` is missing → temporary AP / Serial config asks user to select:
  * **Scenario A:** Mains / tap water
  * **Scenario B:** Raw water pump + 40L pressure tank
* Selection (`Scenario_A` / `Scenario_B`) is saved to NVS; board restarts.

---

## 4. High-Level Operational Modes (مدهای ۳گانه کاری)

Software exposes a **3-state operational mode** (separate from Scenario A/B and from master ON/OFF):

### Mode 1: Active Mode (مد فعال)
* **Condition:** $V_{solar} > V_{start}$ **and** at least one pump motor is ON (Scenario B: Relay1 or Relay3; Scenario A: Relay3 counts as the purification pump; inlet solenoid is not a “pump”).
* **Behavior:** Intake and/or purification routines may run (subject to interlocks and faults). Excess solar charges the battery via Tracer BN.
* **Interlocking rule (motors):** Only **one** pump motor at a time — whenever Relay1 (raw pump) is ACTIVE, Relay3 must be forced OFF.
* **UI:** `حالت فعال (آبگیری)` or `حالت فعال (تصفیه)` according to which pump is ON.

### Mode 2: Standby Mode (مد انتظار)
* **Condition:** $V_{solar} > V_{start}$, but **no** pump is active (e.g. product tank full, waiting for raw water / intake wait, or soft wait states).
* **UI:** `حالت انتظار (دلیل در پرانتز)` — e.g. `حالت انتظار (مخزن پر است)`, `حالت انتظار (عدم دسترسی به آب خام)`.

### Mode 3: Night Mode (مد شب)
* **Condition:** $V_{solar} < V_{start}$ (sunset, heavy overcast, night). Pumps that require solar do not run.
* **Night light (Relay 4)** is evaluated **separately** from Mode 3 entry (battery conservation):
  * **ON:** $V_{solar} < 1.0\text{V}$ continuously for > 3 minutes → Relay4 ON.
  * **OFF:** $V_{solar} > 12.0\text{V}$ continuously for > 3 minutes → Relay4 OFF.
* **UI examples:**
  * $V_{solar} < 18\text{V}$ but not dark enough for lights → `حالت شب (چراغ شب: خاموش)`
  * After darkness debounce → `حالت شب (چراغ شب: روشن)`

### Deep Sleep (sub-state)
* When the system remains in **Standby or Night** with no activity for **15 continuous minutes**: drive GPIO4 high (sensor rail OFF via P-MOSFET), enter ESP32 Deep Sleep, wake every **5 minutes** for telemetry / mode re-evaluation.
* Leak handling: see Master Switch / Leak (interrupt path remains a design constraint; firmware must not leave the plant unsafe on leak).

---

## 5. Default Configurable Constants
Define as named compile-time / NVS-overridable constants at firmware top:

| Symbol | Default | Notes |
| :--- | :--- | :--- |
| $V_{start}$ | 18.0 V | Enter Active/Standby solar band; below → Night Mode |
| $V_{stop}$ | 15.0 V | Stop purification |
| $V_{pump\_start}$ | 18.0 V | Scenario B raw pump solar gate (same as $V_{start}$ unless overridden) |
| TDS1_Limit | 1000 ppm | Inlet quality trip (after 5 s flow verify) |
| Danger_Limit_TDS2 | 150 ppm | Membrane long-term test |
| $t_{flush}$ | 30 s | Flush duration both scenarios |
| Intake wait | 30 min | After TDS1 high, before flush |
| $P_{low}$ | 1.5 bar | Scenario B: start raw pump / min pressure for purify start |
| $P_{high}$ | 3.5 bar | Scenario B: stop raw pump |
| Night light ON | < 1.0 V for 3 min | Relay4 |
| Night light OFF | > 12.0 V for 3 min | Relay4 |
| UV life | 9000 h | NVS every 1 h |
| Pre-filter volume | 5000 L | Estimate from Relay3 runtime × flow |
| Avg pump flow | 1.0 L/min | Volume estimate |
| Dry-run (purify, A) | 30 s / 15 min wait / 3 retries → hard lock | Pressure switch |
| Raw dry-run (B) | 5 min without $P_{high}$ / 30 min wait / 3 consecutive → hard lock | Transducer |

---

## 6. System Work Routines (روال‌های کاری سیستم)

### Locked clarifications (approved)
1. **Flush needs both paths open:** A: Inlet open (Relay1 OFF) **and** Drain open (Relay2 ON). B: Raw pump ON (Relay1 ON) **and** Drain open (Relay2 ON).
2. **5-second flow verification:** TDS1 “high” only after ≥ 5 s continuous flow.
3. **Scenario B motor interlock:** Relay1 ON ⇒ Relay3 forced OFF. Absolute priority when both intake and purify are needed: **intake first** until pressure reaches $P_{high}$, then Relay1 OFF, then purify may start.
4. **Event logging:** Fault/intake/flush events logged; wall-clock date/time deferred until RTC/NTP.
5. **Master software ON/OFF (Web Settings):** `cmd: power` / `systemEnabled`. Boot default **OFF** (Idle). OFF = safe shutdown (purify/night/drain off; A closes inlet Relay1 ON; B stops raw pump). **Leak sensor remains armed 24/7** (including OFF) and still closes inlet / stops pumps and locks on leak.

### A. Water Intake Routine

#### Scenario A (Mains)
* Normal: Inlet open (Relay1 OFF), Drain closed (Relay2 OFF).
* After ≥ 5 s flow, if TDS1 > TDS1_Limit → close inlet (Relay1 ON), log, wait 30 min.
* Flush $t_{flush}$: Inlet open + Drain open; measure TDS1.
* Still dirty → close inlet, repeat 30 min wait. Clean → normal.

#### Scenario B (Raw pump + 40L pressure tank)
* Solar gate: raw pump only if $V_{solar} > V_{pump\_start}$.
* **Pressure hysteresis (GPIO 35 transducer):**
  * $P < P_{low}$ → Relay1 ON (fill). Relay3 forced OFF.
  * $P > P_{high}$ → Relay1 OFF.
* **TDS1 / drain / flush (required — not removed):** While raw pump is ON and water is flowing, after ≥ 5 s, if TDS1 > TDS1_Limit → Relay1 OFF, Relay2 ON (drain 40L + line), wait 30 min, then flush $t_{flush}$ with Relay1 ON **and** Relay2 ON. Still dirty → repeat wait; clean → close drain and resume pressure control.
* **Raw-pump dry-run protection:** If Relay1 runs **5 continuous minutes** without reaching $P_{high}$ → “Water Intake Fault”: Relay1 OFF, 30 min lock/wait with UI countdown `MM:SS` + Reset button (`آیا مطمئنید می‌خواهید وقفه ۳۰ دقیقه‌ای را ریست کنید؟`). On confirm or timeout → retry intake. **3 consecutive** failures → **hard lockout** until physical reset.

### B. Purification Routine
* **Start (Scenario A):** Float = Tank_Low, pressure switch HIGH (> ~2 bar), $V_{solar} > V_{start}$, no active fault/lock, master ON.
* **Start (Scenario B):** Float = Tank_Low, $P \ge P_{low}$, Relay1 OFF, $V_{solar} > V_{start}$, no active fault/lock, master ON. If $P < P_{low}$, intake has absolute priority (purify stays OFF).
* **Action:** Relay3 ON (RO pump + UV together).
* **Stop instantly if:** Tank_Full, $V_{solar} < V_{stop}$, Scenario B Relay1 becomes ACTIVE, or any system fault/lock.
* **Scenario A low pressure while purifying:** if pressure switch open > 30 s → dry-run fault path (Section 7.2). Do not treat a brief open as an instant permanent stop without that timer.

---

## 7. System Faults & Protections

**Logging:** Always log type + key values; wall-clock when RTC/NTP exists.

### 1. Water Leakage
* GPIO 14 LOW → Relay3 OFF; A: Relay1 ON (close inlet); B: Relay1 OFF. Permanent lock. Armed even when master OFF.

### 2. Inlet Low Pressure / Purify Dry-Run (Scenario A)
* Relay3 ON and pressure switch open > 30 s → Relay3 OFF, wait 15 min, retry up to 3×, then hard lock.

### 3. Water Intake Fault / Raw Dry-Run (Scenario B)
* Relay1 ON for 5 min without $P_{high}$ → 30 min wait + UI reset; 3 consecutive → hard lock.

### 4. UV Lamp Life
* Accumulated Relay3 runtime > 9000 h (NVS every 1 h) → stop, lock until manual reset after replacement.

### 5. Pre-Filter Volume
* `Volume ≈ Relay3_Runtime × Avg_Flow` > 5000 L (NVS every 1 h) → stop, lock until reset after service.

### 6. RO Membrane Degradation
* TDS2 > Danger_Limit → 5-step test every 100 L (5 s average each). All 5 fail → lock; any pass → clear warning.

---

## 8. Energy Telemetry (Modbus RTU ↔ Tracer BN)

| Parameter | Value |
| :--- | :--- |
| Interface | UART1 GPIO25/26, isolated RS485, **auto-direction** (no DE/RE pin) |
| Framing | 115200 8N1 |
| Slave ID | 1 |
| $V_{solar}$ (PV voltage) | Register `0x3100`, value ÷ 100 → volts |
| Battery SoC % | Register `0x311A` |

Control thresholds use $V_{solar}$ in volts. UI «میزان تابش» remains a **derived irradiance %** from $V_{solar}$ (shared formula); SoC % is shown on Home from Modbus.

### Current development stub (Phase)
Until the charge controller is wired, firmware shall provide **Modbus stub functions** that return synthetic $V_{solar}$ and SoC so the 3-mode machine, night light, and relays can be tested without hardware.

---

## 9. Scenario B Pressure Transducer (GPIO 35)

* Sensor: **0–5 bar**, output **0.5 V – 4.5 V**.
* Board: 10k/20k divider → scale factor **2/3** into GPIO35; recover sensor volts then pressure:

$$
V_{sensor} = V_{adc\_gpio35} \times 1.5
$$

$$
P_{bar} = (V_{sensor} - 0.5) \times 1.25
$$

$P_{low}$ / $P_{high}$ are configurable (defaults 1.5 / 3.5 bar).

---

## 10. Locked UI Decisions — Home Page (نهایی‌شده)
These decisions are approved for the Web App. Firmware must expose the required live values when available.

### Display that must remain
* **Battery SoC (%):** Home header chip; from Modbus `0x311A` when live, else `--`.
* **Salt rejection (%):** `(1 - TDS_outlet / TDS_inlet) * 100`.
* **UV runtime hours:** from Relay3 accumulator.
* **Irradiance (%):** derived from $V_{solar}$ (Modbus); not raw volts in the chip.
* **Produced volume (L):** `Relay3_Runtime × Avg_Flow`.
* **TDS dual rings** unchanged.
* **Temperature chip:** product-water temp from TDS2 until ambient sensor exists.

### Scenario-aware schematic
* Label Scenario A (mains) or B (raw pump).
* **A path:** Inlet → pre-filter → تصفیه+UV → membrane → product tank (+ drain).
* **B path:** Raw pump → 40L pressure tank → pre-filter → تصفیه+UV → membrane → product tank (+ drain).
* Product tank: Full / Low only (float).
* **B raw tank display:** prefer live pressure band when transducer is live; until then inferred (pump ON ≈ empty / pump OFF ≈ full) is acceptable interim.
* Home bottom: **Active routine / mode box** + separate **Alerts box**.

### Mode / intake-fault UI (locked with Section 4 & 7.3)
* Show 3-mode Persian strings (Active / Standby with reason / Night with light on|off).
* Scenario B intake wait: countdown `MM:SS` + Reset with confirm dialog.

### Explicitly deferred on Home
* Extra chips for every raw digital bit beyond alerts/mode (unless later approved).

---

## 11. Firmware Implementation Roadmap

### Locked development rule — hardware gating
**Do not implement production Modbus/RS485 or production transducer calibration until that hardware is installed.** Switch paths with `#define BENCH_SIMULATION_MODE`.

| Path | Behavior |
| :--- | :--- |
| `BENCH_SIMULATION_MODE 1` (current) | Pot **GPIO34** → $V_{solar}$ (0–3.3 V ADC → 0–60 V), 5-sample MA @ 100 ms. Pot **GPIO35** → tank pressure (0–3.3 V → 0–5 bar), same filter. SoC = fixed stub 80%. WS also publishes top-level `tankPressureBar` / `pressureAdc`. |
| `BENCH_SIMULATION_MODE 0` | Same API (`plantVSolar`, `tankPressureBar`); later fill with Modbus `0x3100` / `0x311A` and production transducer formula (Section 9). Core state machine must not need refactor. |

| Not on board yet | On board / in use now |
| :--- | :--- |
| RS485 + Tracer Modbus | Relays, MOSFET, TDS UART, digital inputs |
| Production 0.5–4.5 V transducer front-end | Benchtop pots GPIO34/35 |
| Deep Sleep policy | Wi-Fi AP, WebSocket, 3-mode control |

### Phase 1 — Current (bench + core SM)
* `#define BENCH_SIMULATION_MODE 1` in `config.h`
* 3-mode machine: Active (solar OK **and** a pump motor ON) / Standby (solar OK, no pump) / Night ($V_{solar} < V_{start}$)
* Night light independent debounce (1 V / 12 V / 3 min)
* Intake B hysteresis $P_{low}$/$P_{high}$ + raw dry-run (5 min / 30 min wait / 3× hard lock) + TDS/flush
* Settings **کادر تست**: live gauges for both pots (number + gauge)
* WS fields: `opMode`, `opModeLabel`, `bench.*`, `intakeWait*`, `cmd: reset_intake_wait`

### Phase 2 — When RS485 + Tracer installed
* Set `BENCH_SIMULATION_MODE 0`; implement Modbus behind the same API.

### Phase 3 — When production transducer installed
* Replace GPIO35 bench mapping with Section 9 formula (may still use GPIO35).

### Phase 4 — Deep Sleep / capability review
* When explicitly requested.
