#ifndef CONFIG_H
#define CONFIG_H

// ===== Serial =====
#define SERIAL_BAUD_RATE 115200

// ===== Relays (PROJECT_BRIEF Section 2 — new mapping) =====
#define RELAY1_PIN   13   // NO: Scenario A inlet solenoid / Scenario B raw DC pump
#define RELAY2_PIN   15   // NC: Drain solenoid (both scenarios)
#define RELAY3_PIN    2   // NO: Purification pump 24V + UV lamp 24V
#define RELAY4_PIN   12   // NO: Night environmental lighting 12V

// P-Channel MOSFET: Active LOW gates VCC to TDS module and Pressure Switch
#define SENSOR_POWER_MOSFET_PIN  4
#define SENSOR_POWER_ACTIVE_LOW  1

// Board relays are Active HIGH (coil energized = ON)
#define RELAY_ACTIVE_HIGH

// ===== Digital inputs (Phase 1) =====
#define PRESSURE_SWITCH_PIN  18   // HIGH = pressure > 2 bar
#define FLOAT_SWITCH_PIN     27   // HIGH = tank full / LOW = tank low (pull-up)
#define LEAK_SENSOR_PIN      14   // Active LOW = water detected (Phase 1 digital)

#define INPUT_DEBOUNCE_MS    50

// ===== Fault timing (Phase 1) =====
#define DRY_RUN_FAULT_MS           30000UL   // 30 s low pressure while purifying
#define DRY_RUN_RETRY_WAIT_MS      900000UL  // 15 minutes
#define DRY_RUN_MAX_RETRIES        3

// ===== TDS module (UART2) — kept from previous work =====
#define TDS_RX_PIN    16
#define TDS_TX_PIN    17
#define TDS_UART_BAUD 9600

// ===== Access Point =====
#define WIFI_AP_SSID     "SolarRO"
#define WIFI_AP_PASSWORD "11223344"

// ===== Phase flags =====
// TODO Phase 2: set to 1 and enforce V_solar start/stop in purification routine
#define PHASE1_IGNORE_VSOLAR  1

#endif // CONFIG_H
