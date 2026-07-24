#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// Solar RO Controller — compile-time configuration (PROJECT_BRIEF)
// =============================================================================

#define SERIAL_BAUD_RATE 115200

// --- Relays (brief §2) -------------------------------------------------------
#define RELAY1_PIN  13   // A: inlet NO / B: raw pump NO
#define RELAY2_PIN  15   // Drain (NC coil sense in brief)
#define RELAY3_PIN   2   // Purification pump + UV
#define RELAY4_PIN  12   // Night lights
#define SENSOR_POWER_MOSFET_PIN 4
#define SENSOR_POWER_ACTIVE_LOW 1
#define RELAY_ACTIVE_HIGH

// --- Digital inputs ----------------------------------------------------------
#define PRESSURE_SWITCH_PIN 18   // HIGH = > ~2 bar
#define FLOAT_SWITCH_PIN    27   // HIGH = tank full
#define LEAK_SENSOR_PIN     14   // Active LOW
#define INPUT_DEBOUNCE_MS   50

// --- Intake (brief §4.A) -----------------------------------------------------
#define TDS1_LIMIT_PPM       1000.0f
#define TDS_FLOW_VERIFY_MS   5000UL
#define INTAKE_WAIT_MS       1800000UL
#define INTAKE_FLUSH_MS_A    30000UL
#define INTAKE_FLUSH_MS_B    30000UL

// --- Solar (Phase1 stub until ADS1115) ---------------------------------------
#define PHASE1_IGNORE_VSOLAR 1
#define V_SOLAR_START        18.0f
#define V_SOLAR_STOP         15.0f
#define V_PUMP_START         18.0f

// --- Faults (brief §5) -------------------------------------------------------
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

// --- TDS UART ---------------------------------------------------------------
#define TDS_RX_PIN    16
#define TDS_TX_PIN    17
#define TDS_UART_BAUD 9600

// --- Wi-Fi AP ---------------------------------------------------------------
#define WIFI_AP_SSID     "SolarRO"
#define WIFI_AP_PASSWORD "11223344"

#endif // CONFIG_H
