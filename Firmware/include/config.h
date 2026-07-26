#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// Solar RO Controller — compile-time configuration (PROJECT_BRIEF)
// =============================================================================

#define SERIAL_BAUD_RATE 115200

// 1 = benchtop pots (GPIO34/35). 0 = production path (Modbus later; stubs until then)
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
#define BENCH_PRESSURE_ADC_PIN 35
#define BENCH_ADC_SAMPLES      20
#define BENCH_ADC_SAMPLE_MS    1000UL
#define BENCH_VSOLAR_MAX_V     60.0f
#define BENCH_PRESSURE_MAX_BAR 5.0f
#define BENCH_SOC_STUB_PCT     80.0f

// Reserved for production (no driver until hardware installed)
// #define RS485_RX_PIN 25
// #define RS485_TX_PIN 26
// Production transducer uses same GPIO35 with different mapping (see brief §9)

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
#define RAW_DRY_MAX_RETRIES  3

// --- Solar thresholds --------------------------------------------------------
#define V_SOLAR_START        18.0f
#define V_SOLAR_STOP         15.0f
#define V_PUMP_START         18.0f
#define NIGHT_LIGHT_ON_V     1.0f
#define NIGHT_LIGHT_OFF_V    12.0f
#define NIGHT_LIGHT_DEBOUNCE_MS 180000UL

// --- Faults ------------------------------------------------------------------
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
