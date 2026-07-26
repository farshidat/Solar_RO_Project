#include "analog_bench.h"
#include "config.h"

static float maPush(float *buf, uint8_t &count, uint8_t &idx, float sample) {
  buf[idx] = sample;
  idx = (uint8_t)((idx + 1) % BENCH_ADC_SAMPLES);
  if (count < BENCH_ADC_SAMPLES) count++;
  float sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += buf[i];
  return sum / (float)count;
}

#if BENCH_SIMULATION_MODE

static float vBuf[BENCH_ADC_SAMPLES];
static float pBuf[BENCH_ADC_SAMPLES];
static uint8_t vCount = 0, vIdx = 0;
static uint8_t pCount = 0, pIdx = 0;
static float vAdcFilt = 0;
static float pAdcFilt = 0;
static float vSolar = 0;
static float tankBar = 0;
static uint32_t lastSampleMs = 0;

static float readAdcVolts(uint8_t pin) {
  // Full-scale ~3.3 V on ESP32 ADC1
  return (analogRead(pin) / 4095.0f) * 3.3f;
}

static void applyMapped() {
  vSolar = (vAdcFilt / 3.3f) * BENCH_VSOLAR_MAX_V;
  tankBar = (pAdcFilt / 3.3f) * BENCH_PRESSURE_MAX_BAR;
  if (vSolar < 0) vSolar = 0;
  if (tankBar < 0) tankBar = 0;
}

void analogBenchInit() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(BENCH_VSOLAR_ADC_PIN, INPUT);
  pinMode(BENCH_PRESSURE_ADC_PIN, INPUT);

  vCount = pCount = 0;
  vIdx = pIdx = 0;
  lastSampleMs = 0;

  // Prime filter so UI is not stuck on stale defaults
  for (uint8_t i = 0; i < BENCH_ADC_SAMPLES; i++) {
    vAdcFilt = maPush(vBuf, vCount, vIdx, readAdcVolts(BENCH_VSOLAR_ADC_PIN));
    pAdcFilt = maPush(pBuf, pCount, pIdx, readAdcVolts(BENCH_PRESSURE_ADC_PIN));
    delay(2);
  }
  applyMapped();
  lastSampleMs = millis();
}

void analogBenchUpdate() {
  const uint32_t now = millis();
  if ((now - lastSampleMs) < BENCH_ADC_SAMPLE_MS) return;
  lastSampleMs = now;

  vAdcFilt = maPush(vBuf, vCount, vIdx, readAdcVolts(BENCH_VSOLAR_ADC_PIN));
  pAdcFilt = maPush(pBuf, pCount, pIdx, readAdcVolts(BENCH_PRESSURE_ADC_PIN));
  applyMapped();
}

float plantVSolar() { return vSolar; }
float plantSocPercent() { return BENCH_SOC_STUB_PCT; }
float tankPressureBar() { return tankBar; }
float benchVSolarAdcVolts() { return vAdcFilt; }
float benchPressureAdcVolts() { return pAdcFilt; }

#else

static float vSolar = 24.0f;
static float tankBar = 2.5f;

void analogBenchInit() {}
void analogBenchUpdate() {}

float plantVSolar() { return vSolar; }
float plantSocPercent() { return BENCH_SOC_STUB_PCT; }
float tankPressureBar() { return tankBar; }
float benchVSolarAdcVolts() { return 0; }
float benchPressureAdcVolts() { return 0; }

#endif

bool plantSolarAbove(float thresholdV) {
  return plantVSolar() > thresholdV;
}
