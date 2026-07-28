#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// Solar RO Controller — compile-time configuration (PROJECT_BRIEF)
// =============================================================================

#define SERIAL_BAUD_RATE 115200

// 1 = benchtop pots. 0 = production path (Modbus later)
#ifndef BENCH_SIMULATION_MODE
#define BENCH_SIMULATION_MODE 1
#endif

// --- Relays (brief §2) -------------------------------------------------------
#define RELAY1_PIN  13
#define RELAY2_PIN  15
#define RELAY3_PIN   2
#define RELAY4_PIN  12
#define SENSOR_POWER_MOSFET_PIN 4
#define SENSOR_POWER_ACTIVE_LOW 1
#define RELAY_ACTIVE_HIGH

// --- Digital inputs ----------------------------------------------------------
#define PRESSURE_SWITCH_PIN 18
#define FLOAT_SWITCH_PIN    27
#define LEAK_SENSOR_PIN     14
#define INPUT_DEBOUNCE_MS   50

// --- Benchtop ADC simulation (temporary) -------------------------------------
#define BENCH_VSOLAR_ADC_PIN   34
// Bench pressure pot: GPIO33 verified working (GPIO35 dead; 32 unused)
#define BENCH_PRESSURE_ADC_PIN 33
#define BENCH_ADC_SAMPLES      3
#define BENCH_ADC_SAMPLE_MS    50UL
#define BENCH_VSOLAR_MAX_V     60.0f
#define BENCH_PRESSURE_MAX_BAR 5.0f
#define BENCH_SOC_STUB_PCT     80.0f

// Reserved for production RS485 (no driver until hardware installed)
// #define RS485_RX_PIN 25
// #define RS485_TX_PIN 26

// --- Intake ------------------------------------------------------------------
#define TDS1_LIMIT_PPM       1000.0f
#define TDS_FLOW_VERIFY_MS   5000UL
#define INTAKE_WAIT_MS       1800000UL
#define INTAKE_FLUSH_MS_A    30000UL
#define INTAKE_FLUSH_MS_B    30000UL
#define P_LOW_BAR            1.5f
#define P_HIGH_BAR           3.5f
#define RAW_DRY_RUN_MS       300000UL
#define RAW_DRY_WAIT_MS      1800000UL
// No hard lock after N retries — cycle repeats until pressure recovers

// --- Irradiance % thresholds (from V_solar / 60 * 100) ------------------------
// Day/Night hysteresis (prevents chatter near the edge)
#define IRR_DAY_ENTER_PCT    35.0f   // Night → day band
#define IRR_DAY_EXIT_PCT     30.0f   // day band → Night
// Night light (Relay4), independent, with small hysteresis
#define IRR_NIGHT_LIGHT_ON_PCT   5.0f
#define IRR_NIGHT_LIGHT_OFF_PCT  8.0f
#define NIGHT_LIGHT_DEBOUNCE_MS  5000UL

// Legacy volt names kept as aliases for docs / older call sites
#define V_SOLAR_START        18.0f
#define V_SOLAR_STOP         15.0f
#define V_PUMP_START         18.0f

// --- Purification Scenario A pressure switch (GPIO18) ------------------------
// Low for this long → stop Relay3. High for this long → allow start again.
#define PURIFY_A_PRESSURE_CONFIRM_MS  5000UL

// --- Faults ------------------------------------------------------------------
// Legacy A dry-run 30s/15m path disabled — replaced by PURIFY_A_PRESSURE_CONFIRM_MS
#define DRY_RUN_FAULT_MS          30000UL
#define DRY_RUN_RETRY_WAIT_MS     900000UL
#define DRY_RUN_MAX_RETRIES       3
#define UV_LIFE_HOURS             9000UL
#define PREFILTER_LIMIT_LITERS    5000.0f
#define AVG_PUMP_FLOW_LPM         1.0f
#define TDS2_DANGER_PPM           150.0f
#define MEMBRANE_TEST_STEP_LITERS 100.0f
#define MEMBRANE_TEST_STEPS       5
#define MEMBRANE_TDS_AVG_MS       5000UL
#define NVS_SAVE_PERIOD_MS        3600000UL

// --- TDS UART ----------------------------------------------------------------
#define TDS_RX_PIN    16
#define TDS_TX_PIN    17
#define TDS_UART_BAUD 9600

// --- Wi-Fi AP ----------------------------------------------------------------
#define WIFI_AP_SSID     "SolarRO"
#define WIFI_AP_PASSWORD "11223344"

#endif // CONFIG_H
