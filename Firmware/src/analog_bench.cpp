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
static float vSolar = 24.0f;
static float tankBar = 2.5f;
static uint32_t lastSampleMs = 0;

void analogBenchInit() {
  analogReadResolution(12);
  vCount = pCount = 0;
  vIdx = pIdx = 0;
  lastSampleMs = 0;
}

void analogBenchUpdate() {
  const uint32_t now = millis();
  if (lastSampleMs != 0 && (now - lastSampleMs) < BENCH_ADC_SAMPLE_MS) return;
  lastSampleMs = now;

  const float vRaw = (analogRead(BENCH_VSOLAR_ADC_PIN) / 4095.0f) * 3.3f;
  const float pRaw = (analogRead(BENCH_PRESSURE_ADC_PIN) / 4095.0f) * 3.3f;
  vAdcFilt = maPush(vBuf, vCount, vIdx, vRaw);
  pAdcFilt = maPush(pBuf, pCount, pIdx, pRaw);

  // 0..3.3 V ADC → 0..60 V solar ; 0..3.3 V → 0..5 bar
  vSolar = (vAdcFilt / 3.3f) * BENCH_VSOLAR_MAX_V;
  tankBar = (pAdcFilt / 3.3f) * BENCH_PRESSURE_MAX_BAR;
}

float plantVSolar() { return vSolar; }
float plantSocPercent() { return BENCH_SOC_STUB_PCT; }
float tankPressureBar() { return tankBar; }
float benchVSolarAdcVolts() { return vAdcFilt; }
float benchPressureAdcVolts() { return pAdcFilt; }

#else

// Production: Modbus / transducer drivers land here later — same API.
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
