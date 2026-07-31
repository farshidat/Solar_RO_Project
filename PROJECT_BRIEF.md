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
| **Pressure Transducer** | Analog Input | GPIO 35 (production) / **GPIO 33 (bench pot, verified)** | Scenario B 40L tank pressure. Production: 0.5–4.5 V via 10k/20k divider + RC on GPIO35 when installed. Benchtop: potentiometer on **GPIO33** (`BENCH_SIMULATION_MODE`). GPIO35 not used on current board. |
| **Pressure Switch** | Digital Input | GPIO 18 | Scenario A mains pressure (> ~2 bar when HIGH) |
| **Leak Sensor** | Digital Input | GPIO 14 | Interrupt. Active Low = water detected |
| **Float Switch** | Digital Input | GPIO 27 | HIGH = product tank full (100%) / LOW = low (< 80%). Pull-up required |

---

## 3. Boot Initialization, Wi-Fi & mDNS
On first boot after firmware upload, ESP32 checks NVS (`sys_mode` / System_Mode):
* If missing → **First-run setup mode** (scenario stays `Unset`; plant routines stay idle):
  * SoftAP SSID: **`Nik-Sun-Purifier`** (password: `11223344`), channel 6, WiFi sleep off, elevated TX power (stability)
  * mDNS: **`Nik-Sun-Purifier.local`**
  * **Captive portal removed (locked):** no OS captive popup / DNS hijack. User opens `http://192.168.4.1` or `http://Nik-Sun-Purifier.local` in the browser
  * User selects **Scenario A** or **Scenario B** in the Web App
* Selection saved to NVS; board **restarts**; `setupNeeded` becomes false.
* If already configured → same SoftAP + mDNS; Web App at the URLs above.
* Future STA (router) mode: keep mDNS hostname `Nik-Sun-Purifier`.
* SoftAP stability note: **no `delay()` / `vTaskDelay()` in `loop` or HTTP/WS handlers**; TDS UART is a non-blocking state machine; WiFi modem sleep disabled in AP mode.
* Static Web assets on LittleFS are pre-gzipped (`.gz` siblings); SoftAP serves gzip when the client accepts it. Browser `Cache-Control: public, max-age=86400` on static files; `/api/status` remains `no-store`.

---

## 4. High-Level Operational Modes (مدهای ۳گانه کاری)

Software exposes a **3-state operational mode** (separate from Scenario A/B and from master ON/OFF):

Irradiance % is derived as $(V_{solar} / 60) \times 100$ (bench) / same scale when Modbus is live.

**Day-band hysteresis (locked — prevents chatter):**
* Enter day band (Night → Active/Standby possible): irradiance **> 35%**
* Leave day band (→ Night): irradiance **< 30%**
* Between 30–35%: hold previous band.

### Mode 1: Active Mode (مد فعال)
* **Condition:** Inside day band **and** at least one pump motor is ON (Scenario B: Relay1 or Relay3; Scenario A: Relay3 is the pump; inlet solenoid is not a “pump”).
* **Behavior:** Intake and/or purification routines may run (subject to interlocks and faults).
* **Interlocking rule (motors):** Only **one** pump motor at a time — Relay1 ON ⇒ Relay3 OFF.
* **UI:** `حالت فعال (آبگیری)` or `حالت فعال (تصفیه)`.

### Mode 2: Standby Mode (مد انتظار)
* **Condition:** Inside day band, but **no** pump is active.
* **UI:** `حالت انتظار (دلیل در پرانتز)`.

### Mode 3: Night Mode (مد شب)
* **Condition:** Outside day band (irradiance dropped below 30% after having been in day, or never yet above 35%).
* **Night light (Relay 4)** — independent of pump modes, **waits, and hard locks**:
  * Only water/purify actuators stop on fault/wait; Relay4 follows irradiance rules regardless.
  * **ON:** irradiance **< 5%** continuously for debounce (default 5 s in bench firmware).
  * **OFF:** irradiance **> 8%** continuously for same debounce (hysteresis vs 5%).
* **UI:** `حالت شب (چراغ شب: روشن|خاموش)`.

### Deep Sleep (sub-state)
* When the system remains in **Standby or Night** with no activity for **15 continuous minutes**: drive GPIO4 high (sensor rail OFF via P-MOSFET), enter ESP32 Deep Sleep, wake every **5 minutes** for telemetry / mode re-evaluation.
* Leak handling: see Master Switch / Leak (interrupt path remains a design constraint; firmware must not leave the plant unsafe on leak).

---

## 5. Default Configurable Constants
Define as named compile-time / NVS-overridable constants at firmware top:

| Symbol | Default | Notes |
| :--- | :--- | :--- |
| Day band enter / leave | irradiance > 35% / < 30% | 3-mode day/night (not raw $V_{start}$) |
| $V_{start}$ | 18.0 V | Purification solar gate (may start when above) |
| $V_{stop}$ | 15.0 V | Stop purification when below |
| $V_{pump\_start}$ | 18.0 V | Scenario B raw pump solar gate (same as $V_{start}$ unless overridden) |
| TDS1_Limit | 1000 ppm | Inlet quality trip (after 5 s flow verify) |
| Danger_Limit_TDS2 | 150 ppm | Membrane long-term test |
| $t_{flush}$ | 30 s | Flush duration both scenarios |
| Intake wait | 30 min | After TDS1 high, before flush |
| $P_{low}$ | 1.5 bar | Scenario B: start raw pump / min pressure for purify start |
| $P_{high}$ | 3.5 bar | Scenario B: stop raw pump |
| Night light ON | irradiance < 5% for 5 s | Relay4 (bench; supersedes old 1 V / 3 min) |
| Night light OFF | irradiance > 8% for 5 s | Relay4 hysteresis |
| UV life | 9000 h | NVS every 1 h |
| Pre-filter volume | 5000 L | Estimate from Relay3 runtime × flow |
| Avg pump flow | 1.0 L/min | Volume estimate |
| Purify pressure (A) | GPIO18 LOW 5 s → stop Relay3; HIGH 5 s → allow start | No 15 m lock |
| Raw dry-run (B) | 5 min without $P_{high}$ → 30 min wait; repeat indefinitely until pressure recovers (no hard lock) | Transducer |

---

## 6. System Work Routines (روال‌های کاری سیستم)

### Locked clarifications (approved)
1. **Flush needs both paths open:** A: Inlet open (Relay1 OFF) **and** Drain open (Relay2 ON). B: Raw pump ON (Relay1 ON) **and** Drain open (Relay2 ON).
2. **5-second flow verification:** TDS1 “high” only after ≥ 5 s continuous flow.
3. **Scenario B motor interlock:** Relay1 ON ⇒ Relay3 forced OFF. Absolute priority when both intake and purify are needed: **intake first** until pressure reaches $P_{high}$, then Relay1 OFF, then purify may start.
4. **Event logging:** Fault/intake/flush events logged with Unix `epoch` when the soft clock is synced (phone `set_time`); otherwise epoch `0` / UI shows `--`. Hardware RTC IC later replaces soft clock without changing the log contract.
5. **Master software ON/OFF (Web Settings):** `cmd: power` / `systemEnabled`. Boot default **OFF** (Idle). OFF = safe shutdown of **water path** (purify/drain off; A closes inlet Relay1 ON; B stops raw pump). **Night light stays independent** (irradiance rules). **Leak sensor remains armed 24/7** (including OFF) — see Section 7 Water Leakage (E101 / O306).
6. **Legacy Scenario A purify dry-run hard lock removed:** the old 30 s low-pressure / 15 min wait / 3-retry `lock_dry_run` path is **not active**. Active path is only the soft 5 s stop / 5 s restart (Section 6.B / 7.2). Enum leftovers in firmware must not be re-armed without updating this brief.
7. **System Reset (ریست سیستم):** Settings button → `cmd: system_reset` (aliases: `technician_reset`, `reset_system`). Confirm text: «ریست سیستم کلیه قفل های سخت را برداشته و حافظه ثبت خطاها را پاک میکند . ادامه میدهید ؟». Clears **all** hard lockouts (including `E101_HARD_LOCK`), zeros leak counters (NVS), clears membrane-test warning state, and **clears the event log NVS ring** (all stored alerts/errors). Then logs `L301`.

### A. Water Intake Routine

#### Scenario A (Mains)
* Normal: Inlet open (Relay1 OFF), Drain closed (Relay2 OFF).
* After ≥ 5 s flow, if TDS1 > TDS1_Limit → close inlet (Relay1 ON), log, wait 30 min.
* Flush $t_{flush}$: Inlet open + Drain open; measure TDS1.
* Still dirty → close inlet, repeat 30 min wait. Clean → normal.

#### Scenario B (Raw pump + 40L pressure tank)
* Solar gate: raw pump only if $V_{solar} > V_{pump\_start}$.
* **Pressure hysteresis** (bench: GPIO33 pot → `tankPressureBar`; production: GPIO35 transducer):
  * $P < P_{low}$ → Relay1 ON (fill). Relay3 forced OFF.
  * $P > P_{high}$ → Relay1 OFF.
* **TDS1 / drain / flush (required — not removed):** While raw pump is ON and water is flowing, after ≥ 5 s, if TDS1 > TDS1_Limit → Relay1 OFF, Relay2 ON (drain 40L + line), wait 30 min, then flush $t_{flush}$ with Relay1 ON **and** Relay2 ON. Still dirty → repeat wait; clean → close drain and resume pressure control.
* **Raw-pump dry-run protection:** If Relay1 runs **5 continuous minutes** without reaching $P_{high}$ → Relay1 OFF, 30 min wait with UI countdown `MM:SS` + Reset button (`آیا مطمئنید می‌خواهید وقفه ۳۰ دقیقه‌ای را ریست کنید؟`). On confirm or timeout → retry intake. **Repeat this cycle indefinitely** until pressure reaches $P_{high}$ (no hard lockout).

### B. Purification Routine
* **Start (Scenario A):** Float = Tank_Low, pressure switch HIGH continuously for **5 s**, day-band OK, no active fault/lock, master ON.
* **Start (Scenario B):** Float = Tank_Low, $P \ge P_{low}$, Relay1 OFF, $V_{solar} > V_{start}$, no active fault/lock, master ON. If $P < P_{low}$, intake has absolute priority (purify stays OFF).
* **Action:** Relay3 ON (RO pump + UV together).
* **Stop instantly if:** Tank_Full, $V_{solar} < V_{stop}$, Scenario B Relay1 becomes ACTIVE, or any system fault/lock.
* **Scenario A low pressure while purifying:** if pressure switch stays LOW for **5 s** → stop Relay3 immediately after that confirm. When pressure returns HIGH for **5 s**, purification may start again (no 15-minute lock).

---

## 7. System Faults, Event Codes & Persistence

### 7.0 API strategy (locked)
* ESP transmits **short event codes only** (`E101`, `W204`, `O302`, …) over WebSocket status and `GET /api/status`.
* **Farsi titles, icons, and colors** are Web App only: **E = red (critical)**, **W = yellow (warning)**, **O/L = info/blue**.
* `GET /api/status` minimal payload:
  ```json
  { "mode": 1, "sub_mode": "Purification", "active_code": "O302", "timer": "29:45", "night_light": false }
  ```
  (`mode`: 1 Active / 2 Standby / 3 Night; `timer` is `MM:SS` or `00:00`).

### 7.1 Event log (NVS circular buffer — last **40**)
**All** event codes (E / W / O / L) are written to the NVS ring with epoch + per-code counter. **Survive reboot.** Cleared only by **System Reset** (then `L301` is logged). No RAM-only event history.
| Code | Meaning |
| :--- | :--- |
| E101 | Water leakage / hard leak lockout |
| E102 | UV lamp life expired (>9000 h) |
| E103 | Pre-filter capacity expired (>5000 L) |
| E104 | RO membrane degradation (5/5 failed) |
| E105 | TDS module UART fault |
| E106 | RS485 Modbus fault (when production Modbus path reports disconnect) |
| W201 | Membrane degradation warning (start of 5-step test) |
| W204 | Raw TDS1 high |
| W205 | Still dirty after flush |
| W207 | Solar panel soiling / efficiency loss (reserved) |
| O301 | Inlet low pressure purify pause (Scenario A; firmware confirm **5 s**) |
| O302 | Raw pump 30-minute wait active |
| O305 | Raw water cleaned after flush |
| O306 | Leak cabinet 20-minute dry-out wait |
| L301 | System reset executed |
| L302 | Hard-lock unlock executed (برداشت قفل) |

### 7.2 *(removed)* — formerly RAM-only codes; all codes now use §7.1 NVS.

### 7.3 Leak SM (E101 / O306) — GPIO14 Active Low, armed 24/7
* **E101 active (not hard lock):** GPIO14 LOW → halt water path only (R3 off; A: R1 ON; B: R1 OFF). `leakPhase=active`. **No NVS E101 yet; no hard lock from a single sighting.** Relay4 unchanged.
* **O306:** GPIO14 HIGH → count **one** leak episode, start 20 min dry-out; emit `O306` (NVS). During O306, leak input is **display-only**.
* UI Reset → `cmd: reset_leak_wait` (timer zeros; then threshold check).
* **Hard lock only if** `Leak_Count_24H >= 3` **or** `Leak_Count_Total >= 10` after an episode ends → `leakPhase=hard`, NVS `E101`. Clear via **برداشت قفل** or **ریست سیستم**.

### 7.4 Other protections
* **E102 / E103 / E104:** UV / prefilter / membrane hard locks.
* **O301:** Scenario A pressure switch LOW 5 s while purifying → stop Relay3; HIGH 5 s may restart.
* **O302:** Scenario B raw dry-run 5 min without $P_{high}$ → 30 min wait (repeat, no hard lock).
* **Night light:** never forced off by waits or hard locks — only water/purify actuators stop.

### 7.5 Unlock vs System Reset
* **برداشت قفل** → `cmd: unlock` / `clear_lock`: clears hard lockouts, **zeros hard-lock counters** (`Leak_Count_24H` / `Leak_Count_Total`), keeps event history; logs **L302**.
* **ریست سیستم** → `cmd: system_reset`: performs the same hard unlock + counter zero as above, **and** clears **entire** event NVS log; then emits **L301**. Confirm: «ریست سیستم کلیه قفل های سخت را برداشته و حافظه ثبت خطاها را پاک میکند . ادامه میدهید ؟»
* **W206 removed** — night-mode transition is not announced/logged.

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

## 9. Scenario B Pressure Sensing

### Benchtop (current — verified)
* Potentiometer on **GPIO 33** (`BENCH_PRESSURE_ADC_PIN`), mapping 0–3.3 V ADC → 0–5 bar.
* **Locked after hardware test:** GPIO 33 works on this board. **GPIO 35 does not** (abandoned for bench). Any remaining UI/debug labels must say GPIO 33 (Settings test panel itself was removed).

### Production transducer (later, when installed)
* Sensor: **0–5 bar**, output **0.5 V – 4.5 V**.
* Board: 10k/20k divider → scale factor **2/3** into **GPIO 35**; recover sensor volts then pressure:

$$
V_{sensor} = V_{adc\_gpio35} \times 1.5
$$

$$
P_{bar} = (V_{sensor} - 0.5) \times 1.25
$$

$P_{low}$ / $P_{high}$ are configurable (defaults 1.5 / 3.5 bar).

---

## 10. Locked UI Decisions — Web App (نهایی‌شده)
These decisions are approved for the Web App. Firmware must expose the required live values when available. After UI edits, keep `Webapp/` and `Firmware/data/` identical.

### Home — display that must remain
* **Battery SoC (%):** header chip; from Modbus `0x311A` when live, else `--` (bench stub 80%).
* **Salt rejection (%):** `(1 - TDS_outlet / TDS_inlet) * 100`.
* **UV runtime hours:** from Relay3 accumulator (mock until live counter wired in UI).
* **Irradiance (%):** derived from $V_{solar}$; not raw volts in the chip.
* **Produced volume (L):** `Relay3_Runtime × Avg_Flow` (mock until live).
* **TDS dual rings** unchanged.
* **Temperature chip:** product-water temp from TDS2 until ambient sensor exists.

### Scenario A/B selector (locked)
* **Header:** scenario label is **display-only** (no dropdown / no change) on all pages.
* **Change only on Settings:** dedicated A/B control on the Settings main view; sends `cmd: scenario`.
* Schematic and labels follow live `scenario` from WS/NVS.

### Scenario-aware schematic
* Label Scenario A (mains) or B (raw pump).
* **A path:** Inlet → pre-filter → تصفیه+UV → membrane → product tank (+ drain).
* **B path:** Raw pump → 40L pressure tank → pre-filter → تصفیه+UV → membrane → product tank (+ drain).
* Product tank: **پر / کم** from float (`inputs.tankFull`).
* **B raw tank display:** prefer live pressure band ($P_{high}$ / $P_{low}$ hysteresis); else inferred (pump ON ≈ empty / pump OFF ≈ full) when system on; when system off, pressure/switch logic as implemented.
* Home bottom stack:
  1. Box title **کارکرد کنونی** (3-mode / wait text) only.
  2. **هشدارها و خطاها box removed from Home** — alerts live on the Alerts page.

### Mode / intake-wait / leak-wait UI (locked with Section 4 & 7.1 / 7.3)
* Show 3-mode Persian strings (Active / Standby with reason / Night with light on|off) from `opMode` / `opModeLabel` / `standbyReason` / `nightLight`.
* Scenario B raw dry-run wait (`intakeWaitActive`):
  * Show label **وقفه** + **Reset** only while wait is active.
  * Show countdown `MM:SS` **only while counting** (`intakeWaitSec` / `intakeWaitMs` > 0); hide timer chrome at zero / after reset (no empty timer box).
  * Reset → confirm dialog → `cmd: reset_intake_wait`.
* Leak dry-out wait (`leakWaitActive` / `O306_LEAK_WAIT`): same **وقفه** + countdown + **Reset** pattern; confirm → `cmd: reset_leak_wait`.

### Alerts page & reports (locked)
* **Alerts page:** scrollable box titled **آخرین هشدارها**; **Export** button always pinned at the bottom of the page (outside the scroll box).
* Each alert row shows **code** (when present) + Persian title; datetime row has **time on the left** and **Jalali date on the right**.
* **PDF / Export reports:** include event **codes** with titles; same time-left / date-right ordering.
* **Alerts page:** show **all** NVS event history (E / W / O / L), not only hard locks.
* **Alerts PDF must be multi-page:** export the same full list; codes + Farsi; never truncate to one page.

### Performance page (locked)
* Horizontal industrial gauges (scale + segmented track + yellow pointer + LCD + Status pill) using **project zone colors**.
* Include live/bench: فشار منبع (0–5 bar, zones 1.5 / 3.5), میزان تابش, ولتاژ پنل; plus filter/UV/temp rows as available.
* Tank levels: **binary پر / خالی** only; place near bottom of the gauges card.
* **Bottom box — کارکرد کنونی:** a separate `status-box` under the gauges (same visual language as Home), with the same live mode label plus an elapsed timer that **resets to 00:00** whenever that mode becomes active and counts up while it stays active.
* **Do not** show pH or pump-status rows on this page.

### Settings — main (locked)
* Buttons: **کالیبراسیون**, **تعویض فیلتر**, **تاریخ و ساعت**, **برداشت قفل**, **ریست سیستم** (each with icon).
* **برداشت قفل:** confirm → `cmd: unlock` (Section 7.5).
* **ریست سیستم:** confirm → `cmd: system_reset` (Section 7.5 / L301).
* Scenario A/B selector + master power toggle.
* **کادر تست removed** from Settings (no digital/bench test panel on this page).
* **Clock bar (top of Settings main):**
  * No title/label (تیتر «تاریخ و ساعت» روی کادر نیست؛ فقط دکمه جداگانه برای تنظیم دستی).
  * Live Jalali **date** (day + **Persian month name** + year) and **time**; time aligned to the **right** of the bar.
  * If soft clock not synced yet → date shows `همگام‌سازی نشده`.

### Calibration flow (locked)
* Calibration button → picker page with:
  1. سنسور TDS و دمای آب → EC/temp channels 1–2 (existing TDS module cmds)
  2. ترانسدیوسر فشار → enter reference bar; firmware one-point maps current ADC to that value (`cmd: calibrate_pressure`)
  3. ولتاژ پنل → enter reference volts; one-point (`cmd: calibrate_vsolar`)
  4. سنسور دما (محیط) → placeholder until ambient sensor hardware exists
* Pressure/panel scales persist in NVS (`benchcal`).
* Date/time (**soft clock until hardware RTC**): on every WebSocket connect the phone auto-sends `cmd: set_time` with Unix `epoch` (`auto: true`). ESP keeps wall time via `settimeofday` until power loss. Manual Settings → تاریخ و ساعت still works. Hardware RTC IC later replaces this without changing the WS contract.
* **Jalali display (UI only):** event log stores Unix `epoch` on each entry when clock is synced; Web App alerts list + PDF reports show times with `fa-IR-u-ca-persian` (month name via `month: 'long'`; no extra JS library).

### UI performance (locked — lag control)
* Frontend: batch paints with `requestAnimationFrame`; patch gauges / avoid full DOM rebuild every WS tick; schematic rebuild only when topology key changes.
* Single WS reconnect timer; close previous socket before reconnect.

### Explicitly deferred on Home
* Extra chips for every raw digital bit beyond alerts/mode (unless later approved).

---

## 11. Firmware Implementation Roadmap

### Locked development rule — hardware gating
**Do not implement production Modbus/RS485 or production transducer calibration until that hardware is installed.** Switch paths with `#define BENCH_SIMULATION_MODE`.

| Path | Behavior |
| :--- | :--- |
| `BENCH_SIMULATION_MODE 1` (current) | Pot **GPIO34** → $V_{solar}$ (0–3.3 V → 0–60 V), 3-sample MA @ 50 ms. Pot **GPIO33** → tank pressure (0–3.3 V → 0–5 bar); **GPIO35 abandoned** on this board (not used). SoC stub 80%. Day-band 35%/30%; night light <5% / >8% @ 5 s. |
| `BENCH_SIMULATION_MODE 0` | Same API (`plantVSolar`, `tankPressureBar`); later fill with Modbus `0x3100` / `0x311A` and production transducer formula (Section 9). Core state machine must not need refactor. |

| Not on board yet | On board / in use now |
| :--- | :--- |
| RS485 + Tracer Modbus | Relays, MOSFET, TDS UART, digital inputs |
| Production 0.5–4.5 V transducer front-end | Benchtop pots **GPIO34** (solar) + **GPIO33** (pressure) |
| Deep Sleep policy | Wi-Fi AP, WebSocket, 3-mode control |

### Phase 1 — Current (bench + core SM)
* `#define BENCH_SIMULATION_MODE 1` in `config.h`
* SoftAP `Nik-Sun-Purifier` + mDNS; **no captive portal**
* Soft clock: auto `set_time` on WS connect; event `epoch` + Jalali UI/PDF
* 3-mode machine: Active (day band **and** a pump motor ON) / Standby (day band, no pump) / Night (outside day band)
* Night light independent debounce (irradiance 5% / 8% / 5 s)
* Scenario A purify: pressure switch **5 s** soft stop/start (**no** legacy 15 m / `lock_dry_run`)
* Intake B hysteresis $P_{low}$/$P_{high}$ + raw dry-run (5 min / 30 min wait, **repeat, no hard lock**) + TDS/flush
* Settings: calibration picker (TDS / pressure / panel V / ambient stub) + date/time button; live Jalali clock bar (no title; Persian month name; time on right); no test panel
* Hard locks: E101 hard lock / UV / prefilter / membrane. Soft: E101 active + O306 20 min wait, A pressure, B dry-run wait, membrane warn, intake TDS cycles
* **Loop / telemetry (lag control — locked):**
  * Zero blocking in `loop`: `yield()` only (no `delay` / `vTaskDelay`).
  * Digital inputs (leak / float / pressure switch): poll every **100 ms**.
  * Modbus RS485 Tracer BN: poll every **1000 ms** (stub while `BENCH_SIMULATION_MODE`).
  * Dual TDS UART: non-blocking poll every **2000 ms**, one channel per cycle, **300 ms** wait budget; keep last good sample on timeout. Calibration is async (no multi-second blocks in the WS handler).
  * WS status: change-detect + ≤5 Hz cap + ≥1 Hz heartbeat while clients connected; force send on commands (`power`, `scenario`, `reset_intake_wait`, `reset_leak_wait`, `technician_reset`, …). Heartbeats include `events[]`.
  * Slim JSON (codes only — Farsi on Web App): top-level `vSolar`, `irradiancePct`, `soc`, `tankPressureBar`, `pressureAdc`, `vSolarAdc`, `opMode*`, `intakeWait*`, `leakPhase`, `leakWait*`, `leakCount24h`, `leakCountTotal`, `epoch`, `inputs`, `relays`, `pumps`, `tds*`, `events` (code/count/ms/epoch). Reused static `JsonDocument` + fixed `char[]` buffers (no Arduino `String` for status).
* WS commands: `cmd: power` / `system`, `scenario`, `reset_intake_wait`, `reset_leak_wait`, `unlock` / `clear_lock`, `system_reset`, `calibrate_*`, `set_time` (`auto`).
* HTTP: `GET /api/status` (Section 7.0). WS also carries `mode` / `sub_mode` / `active_code` / `timer` / `night_light` plus telemetry; `events[]` = full NVS history for Alerts page.
* Status JSON buffer sized to fit full events list; heartbeats **include `events`** so Alerts is not empty until the next gen bump.
* Event persistence: NVS ring **40** — **all** codes (Section 7.1). No RAM-only event log.

### Phase 2 — When RS485 + Tracer installed
* Set `BENCH_SIMULATION_MODE 0`; implement Modbus behind the same API.

### Phase 3 — When production transducer installed
* Replace GPIO35 bench mapping with Section 9 formula (may still use GPIO35).

### Phase 4 — Deep Sleep / capability review
* When explicitly requested.
