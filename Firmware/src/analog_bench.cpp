#include "analog_bench.h"
#include "config.h"
#include <Preferences.h>

static Preferences benchPrefs;
static float vScale = BENCH_VSOLAR_MAX_V / 3.3f;       // volts per ADC-volt
static float pScale = BENCH_PRESSURE_MAX_BAR / 3.3f;   // bar per ADC-volt

static float maPush(float *buf, uint8_t &count, uint8_t &idx, float sample) {
  buf[idx] = sample;
  idx = (uint8_t)((idx + 1) % BENCH_ADC_SAMPLES);
  if (count < BENCH_ADC_SAMPLES) count++;
  float sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += buf[i];
  return sum / (float)count;
}

static void loadScales() {
  if (!benchPrefs.begin("benchcal", false)) return;
  vScale = benchPrefs.getFloat("vScale", BENCH_VSOLAR_MAX_V / 3.3f);
  pScale = benchPrefs.getFloat("pScale", BENCH_PRESSURE_MAX_BAR / 3.3f);
  benchPrefs.end();
  if (vScale < 0.1f) vScale = BENCH_VSOLAR_MAX_V / 3.3f;
  if (pScale < 0.01f) pScale = BENCH_PRESSURE_MAX_BAR / 3.3f;
}

static void saveScales() {
  if (!benchPrefs.begin("benchcal", false)) return;
  benchPrefs.putFloat("vScale", vScale);
  benchPrefs.putFloat("pScale", pScale);
  benchPrefs.end();
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
static bool dayBand = false;
static uint32_t lastSampleMs = 0;

static float readAdcVolts(uint8_t pin) {
  analogRead(pin);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += (uint32_t)analogRead(pin);
  return (sum / (4.0f * 4095.0f)) * 3.3f;
}

static void applyMapped() {
  vSolar = vAdcFilt * vScale;
  tankBar = pAdcFilt * pScale;
  if (vSolar < 0) vSolar = 0;
  if (tankBar < 0) tankBar = 0;

  const float irr = plantIrradiancePct();
  if (dayBand) {
    if (irr < IRR_DAY_EXIT_PCT) dayBand = false;
  } else {
    if (irr > IRR_DAY_ENTER_PCT) dayBand = true;
  }
}

void analogBenchInit() {
  loadScales();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(BENCH_VSOLAR_ADC_PIN, INPUT);
  pinMode(BENCH_PRESSURE_ADC_PIN, INPUT);

  vCount = pCount = 0;
  vIdx = pIdx = 0;
  dayBand = false;
  lastSampleMs = 0;

  for (uint8_t i = 0; i < BENCH_ADC_SAMPLES; i++) {
    vAdcFilt = maPush(vBuf, vCount, vIdx, readAdcVolts(BENCH_VSOLAR_ADC_PIN));
    pAdcFilt = maPush(pBuf, pCount, pIdx, readAdcVolts(BENCH_PRESSURE_ADC_PIN));
    yield();
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
float plantIrradiancePct() {
  return (vSolar / BENCH_VSOLAR_MAX_V) * 100.0f;
}
float plantSocPercent() { return BENCH_SOC_STUB_PCT; }
bool plantDayBandActive() { return dayBand; }
float tankPressureBar() { return tankBar; }
float benchVSolarAdcVolts() { return vAdcFilt; }
float benchPressureAdcVolts() { return pAdcFilt; }

bool analogBenchCalibratePressure(float referenceBar) {
  if (!(referenceBar >= 0.0f && referenceBar <= 10.0f)) return false;
  if (pAdcFilt < 0.02f) return false;
  pScale = referenceBar / pAdcFilt;
  saveScales();
  applyMapped();
  return true;
}

bool analogBenchCalibrateVSolar(float referenceVolts) {
  if (!(referenceVolts >= 0.0f && referenceVolts <= 100.0f)) return false;
  if (vAdcFilt < 0.02f) return false;
  vScale = referenceVolts / vAdcFilt;
  saveScales();
  applyMapped();
  return true;
}

#else

static float vSolar = 24.0f;
static float tankBar = 2.5f;
static bool dayBand = true;

void analogBenchInit() {
  loadScales();
  dayBand = true;
}
void analogBenchUpdate() {
  const float irr = plantIrradiancePct();
  if (dayBand) {
    if (irr < IRR_DAY_EXIT_PCT) dayBand = false;
  } else {
    if (irr > IRR_DAY_ENTER_PCT) dayBand = true;
  }
}

float plantVSolar() { return vSolar; }
float plantIrradiancePct() {
  return (vSolar / BENCH_VSOLAR_MAX_V) * 100.0f;
}
float plantSocPercent() { return BENCH_SOC_STUB_PCT; }
bool plantDayBandActive() { return dayBand; }
float tankPressureBar() { return tankBar; }
float benchVSolarAdcVolts() { return 0; }
float benchPressureAdcVolts() { return 0; }

bool analogBenchCalibratePressure(float referenceBar) {
  if (!(referenceBar >= 0.0f && referenceBar <= 10.0f)) return false;
  tankBar = referenceBar;
  return true;
}

bool analogBenchCalibrateVSolar(float referenceVolts) {
  if (!(referenceVolts >= 0.0f && referenceVolts <= 100.0f)) return false;
  vSolar = referenceVolts;
  return true;
}

#endif

bool plantSolarAbove(float thresholdV) {
  (void)thresholdV;
  return plantDayBandActive();
}
