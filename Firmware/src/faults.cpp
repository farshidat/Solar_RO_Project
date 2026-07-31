#include "faults.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "event_log.h"
#include <Preferences.h>
#include <time.h>

static FaultId active = FAULT_NONE;
static bool locked = false;

static LeakPhase leakPhase = LEAK_PHASE_CLEAR;
static uint32_t leakActiveSinceMs = 0;
static uint32_t leakWaitUntilMs = 0;
static uint16_t leakCountTotal = 0;
static uint32_t leakEpochs[LEAK_EPOCH_CAP];
static uint8_t leakEpochHead = 0;
static uint8_t leakEpochCount = 0;

static uint32_t dryLowSince = 0;
static bool dryTiming = false;
static uint8_t dryRetries = 0;
static bool inDryWait = false;
static uint32_t dryWaitUntil = 0;

static uint64_t uvOnMsTotal = 0;
static uint32_t uvSegmentStart = 0;
static bool uvSegmentOpen = false;
static float prefilterLiters = 0.0f;
static uint32_t lastR3SampleMs = 0;
static uint32_t lastNvsSaveMs = 0;

static bool memTestActive = false;
static uint8_t memHighSteps = 0;
static float memLitersSinceStep = 0.0f;
static bool memAvgActive = false;
static uint32_t memAvgStart = 0;
static float memAvgSum = 0;
static uint16_t memAvgCount = 0;

static Preferences prefs;

static void actuatorsSafeShutdown() {
  purificationOff();
  nightLightOff();
  relay2Off();
  if (scenarioIsA()) relay1On();
  else relay1Off();
}

static uint32_t wallEpochOrZero() {
  time_t t = time(nullptr);
  return (t > 1700000000L) ? (uint32_t)t : 0;
}

static uint16_t countLeaksIn24h(uint32_t nowEpoch) {
  if (nowEpoch == 0 || leakEpochCount == 0) return 0;
  uint16_t n = 0;
  for (uint8_t i = 0; i < leakEpochCount; i++) {
    uint8_t idx = (uint8_t)((leakEpochHead + LEAK_EPOCH_CAP - leakEpochCount + i) % LEAK_EPOCH_CAP);
    uint32_t e = leakEpochs[idx];
    if (e != 0 && nowEpoch >= e && (nowEpoch - e) <= LEAK_24H_WINDOW_SEC) n++;
  }
  return n;
}

static void pushLeakEpoch(uint32_t epoch) {
  leakEpochs[leakEpochHead] = epoch;
  leakEpochHead = (uint8_t)((leakEpochHead + 1) % LEAK_EPOCH_CAP);
  if (leakEpochCount < LEAK_EPOCH_CAP) leakEpochCount++;
}

static void clearLeakEpochs() {
  leakEpochHead = 0;
  leakEpochCount = 0;
  for (uint8_t i = 0; i < LEAK_EPOCH_CAP; i++) leakEpochs[i] = 0;
}

static void saveNvs();

static void enterLeakHardLock(const char *logMsg) {
  leakPhase = LEAK_PHASE_HARD_LOCK;
  leakWaitUntilMs = 0;
  active = FAULT_LEAK;
  locked = true;
  actuatorsSafeShutdown();
  eventLogAdd(logMsg ? logMsg : "E101_HARD_LOCK");
  saveNvs();
}

void faultsForceLock(FaultId id, const char *logMsg) {
  active = id;
  locked = true;
  if (id == FAULT_LEAK) leakPhase = LEAK_PHASE_HARD_LOCK;
  actuatorsSafeShutdown();
  eventLogAdd(logMsg ? logMsg : "lock");
}

static void lockFault(FaultId id, const char *logMsg) {
  faultsForceLock(id, logMsg);
}

static void loadNvs() {
  prefs.begin("solar_ro", true);
  uvOnMsTotal = prefs.getULong64("uv_ms", 0);
  prefilterLiters = prefs.getFloat("filt_l", 0);
  memTestActive = prefs.getBool("mem_on", false);
  memHighSteps = prefs.getUChar("mem_st", 0);
  leakCountTotal = (uint16_t)prefs.getUShort("leak_tot", 0);
  leakEpochCount = prefs.getUChar("leak_n", 0);
  leakEpochHead = prefs.getUChar("leak_hd", 0);
  if (leakEpochCount > LEAK_EPOCH_CAP) leakEpochCount = LEAK_EPOCH_CAP;
  if (leakEpochHead >= LEAK_EPOCH_CAP) leakEpochHead = 0;
  for (uint8_t i = 0; i < LEAK_EPOCH_CAP; i++) {
    char key[12];
    snprintf(key, sizeof(key), "le%u", (unsigned)i);
    leakEpochs[i] = prefs.getULong(key, 0);
  }
  const bool hard = prefs.getBool("leak_hard", false);
  prefs.end();

  if (hard) {
    leakPhase = LEAK_PHASE_HARD_LOCK;
    active = FAULT_LEAK;
    locked = true;
  }
}

static void saveNvs() {
  prefs.begin("solar_ro", false);
  prefs.putULong64("uv_ms", uvOnMsTotal);
  prefs.putFloat("filt_l", prefilterLiters);
  prefs.putBool("mem_on", memTestActive);
  prefs.putUChar("mem_st", memHighSteps);
  prefs.putUShort("leak_tot", leakCountTotal);
  prefs.putUChar("leak_n", leakEpochCount);
  prefs.putUChar("leak_hd", leakEpochHead);
  prefs.putBool("leak_hard", leakPhase == LEAK_PHASE_HARD_LOCK);
  for (uint8_t i = 0; i < LEAK_EPOCH_CAP; i++) {
    char key[12];
    snprintf(key, sizeof(key), "le%u", (unsigned)i);
    prefs.putULong(key, leakEpochs[i]);
  }
  prefs.end();
  lastNvsSaveMs = millis();
}

void faultsInit() {
  active = FAULT_NONE;
  locked = false;
  leakPhase = LEAK_PHASE_CLEAR;
  leakActiveSinceMs = 0;
  leakWaitUntilMs = 0;
  dryTiming = false;
  dryRetries = 0;
  inDryWait = false;
  uvSegmentOpen = false;
  memAvgActive = false;
  clearLeakEpochs();
  lastR3SampleMs = millis();
  loadNvs();
  lastNvsSaveMs = millis();
}

const char *faultsName(FaultId id) {
  if (id == FAULT_LEAK) {
    switch (leakPhase) {
      case LEAK_PHASE_ACTIVE: return "E101_ACTIVE";
      case LEAK_PHASE_WAIT: return "O306_LEAK_WAIT";
      case LEAK_PHASE_HARD_LOCK: return "E101_HARD_LOCK";
      default: return "leak";
    }
  }
  switch (id) {
    case FAULT_DRY_RUN: return "dry_run";
    case FAULT_INTAKE_DRY: return "intake_dry";
    case FAULT_UV: return "uv";
    case FAULT_PREFILTER: return "prefilter";
    case FAULT_MEMBRANE: return "membrane";
    default: return "none";
  }
}

static void accumulateRuntime(uint32_t now) {
  const bool r3 = purificationIsOn();
  if (r3 && !uvSegmentOpen) {
    uvSegmentStart = now;
    uvSegmentOpen = true;
  } else if (!r3 && uvSegmentOpen) {
    uvOnMsTotal += (now - uvSegmentStart);
    uvSegmentOpen = false;
  }

  uint32_t dt = now - lastR3SampleMs;
  lastR3SampleMs = now;
  if (r3 && dt > 0 && dt < 10000) {
    float addL = (AVG_PUMP_FLOW_LPM / 60000.0f) * (float)dt;
    prefilterLiters += addL;
    if (memTestActive) memLitersSinceStep += addL;
  }

  if ((now - lastNvsSaveMs) >= NVS_SAVE_PERIOD_MS) {
    if (uvSegmentOpen) {
      uvOnMsTotal += (now - uvSegmentStart);
      uvSegmentStart = now;
    }
    saveNvs();
  }
}

static void updateMembrane(uint32_t now, float tds2Ppm, bool tds2Valid) {
  if (!tds2Valid) return;

  if (!memTestActive) {
    if (tds2Ppm > TDS2_DANGER_PPM) {
      memTestActive = true;
      memHighSteps = 0;
      memLitersSinceStep = 0;
      memAvgActive = false;
      eventLogAdd("membrane_warn");
      saveNvs();
    }
    return;
  }

  if (memLitersSinceStep < MEMBRANE_TEST_STEP_LITERS) return;

  if (!memAvgActive) {
    memAvgActive = true;
    memAvgStart = now;
    memAvgSum = 0;
    memAvgCount = 0;
  }

  if ((now - memAvgStart) < MEMBRANE_TDS_AVG_MS) {
    memAvgSum += tds2Ppm;
    memAvgCount++;
    return;
  }

  float avg = (memAvgCount > 0) ? (memAvgSum / memAvgCount) : tds2Ppm;
  memAvgActive = false;
  memLitersSinceStep = 0;

  if (avg > TDS2_DANGER_PPM) {
    memHighSteps++;
    char buf[40];
    snprintf(buf, sizeof(buf), "membrane_step_%u", (unsigned)memHighSteps);
    eventLogAdd(buf);
    if (memHighSteps >= MEMBRANE_TEST_STEPS) {
      lockFault(FAULT_MEMBRANE, "lock_membrane");
    }
    saveNvs();
  } else {
    memTestActive = false;
    memHighSteps = 0;
    eventLogAdd("membrane_clear");
    saveNvs();
  }
}

/** End O306 wait: hard-lock if thresholds met, else recover. */
static void finishLeakWait(bool fromUiReset) {
  const uint32_t ep = wallEpochOrZero();
  const uint16_t c24 = countLeaksIn24h(ep ? ep : (millis() / 1000UL));
  if (c24 >= LEAK_COUNT_24H_LIMIT || leakCountTotal >= LEAK_COUNT_TOTAL_LIMIT) {
    enterLeakHardLock("10x Leakage Hard Lockout Triggered");
    return;
  }

  leakPhase = LEAK_PHASE_CLEAR;
  leakWaitUntilMs = 0;
  if (active == FAULT_LEAK) {
    active = FAULT_NONE;
    locked = false;
  }
  eventLogAdd(fromUiReset ? "O306_leak_wait_reset" : "E101_recovered");
  saveNvs();
}

/** E101 / O306 leak state machine — armed even when master OFF. */
static void updateLeakProtection(uint32_t now, bool leakLow) {
  if (leakPhase == LEAK_PHASE_HARD_LOCK) {
    actuatorsSafeShutdown();
    active = FAULT_LEAK;
    locked = true;
    return;
  }

  if (leakLow) {
    if (leakPhase != LEAK_PHASE_ACTIVE) {
      leakPhase = LEAK_PHASE_ACTIVE;
      leakActiveSinceMs = now;
      active = FAULT_LEAK;
      locked = true;
      eventLogAdd("E101_ACTIVE");
    }
    actuatorsSafeShutdown();
    return;
  }

  // GPIO HIGH — water cleared
  if (leakPhase == LEAK_PHASE_ACTIVE) {
    const uint32_t durSec = (now - leakActiveSinceMs) / 1000UL;
    const uint32_t ep = wallEpochOrZero();
    pushLeakEpoch(ep ? ep : now / 1000UL);  // fallback: coarse uptime stamp
    if (leakCountTotal < 0xFFFF) leakCountTotal++;

    const uint16_t c24 = countLeaksIn24h(ep ? ep : (now / 1000UL));
    char buf[EVENT_MSG_LEN];
    snprintf(buf, sizeof(buf), "E101 dur=%lus c24=%u tot=%u",
             (unsigned long)durSec, (unsigned)c24, (unsigned)leakCountTotal);
    eventLogAdd(buf);

    leakPhase = LEAK_PHASE_WAIT;
    leakWaitUntilMs = now + LEAK_WAIT_MS;
    active = FAULT_LEAK;
    locked = true;
    actuatorsSafeShutdown();
    eventLogAdd("O306_LEAK_WAIT");
    saveNvs();
    return;
  }

  if (leakPhase == LEAK_PHASE_WAIT) {
    actuatorsSafeShutdown();
    active = FAULT_LEAK;
    locked = true;
    if (now < leakWaitUntilMs) return;
    finishLeakWait(false);
    return;
  }
}

void faultsUpdate(bool systemEnabled) {
  const AppSensors s = appStateSensors();
  const uint32_t now = millis();

  accumulateRuntime(now);

  // Leak armed 24/7 (including master OFF)
  updateLeakProtection(now, s.leak);

  if (leakPhase == LEAK_PHASE_ACTIVE ||
      leakPhase == LEAK_PHASE_WAIT ||
      leakPhase == LEAK_PHASE_HARD_LOCK) {
    return;
  }

  if (locked) {
    actuatorsSafeShutdown();
    return;
  }

  if (!systemEnabled) {
    dryTiming = false;
    return;
  }

  // Scenario A low-pressure stop/start is handled in purify (5 s confirm).
  dryTiming = false;

  if (faultsUvHours() >= (float)UV_LIFE_HOURS) {
    lockFault(FAULT_UV, "lock_uv");
    return;
  }

  if (prefilterLiters >= PREFILTER_LIMIT_LITERS) {
    lockFault(FAULT_PREFILTER, "lock_prefilter");
    return;
  }

  updateMembrane(now, s.tds2Ppm, s.tds2Valid);
}

bool faultsIsLocked() { return locked; }
bool faultsInDryRunWait() { return inDryWait; }
FaultId faultsActive() { return active; }
LeakPhase faultsLeakPhase() { return leakPhase; }

uint32_t faultsLeakWaitMsRemaining() {
  if (leakPhase != LEAK_PHASE_WAIT) return 0;
  uint32_t now = millis();
  if (now >= leakWaitUntilMs) return 0;
  return leakWaitUntilMs - now;
}

uint16_t faultsLeakCount24h() {
  uint32_t ep = wallEpochOrZero();
  if (!ep) ep = millis() / 1000UL;
  return countLeaksIn24h(ep);
}

uint16_t faultsLeakCountTotal() { return leakCountTotal; }
uint8_t faultsDryRunRetries() { return dryRetries; }
bool faultsMembraneTestActive() { return memTestActive; }
uint8_t faultsMembraneTestStep() { return memHighSteps; }

float faultsUvHours() {
  uint64_t ms = uvOnMsTotal;
  if (uvSegmentOpen) ms += (millis() - uvSegmentStart);
  return (float)ms / 3600000.0f;
}

float faultsPrefilterLiters() { return prefilterLiters; }

void faultsResetUvCounter() {
  uvOnMsTotal = 0;
  uvSegmentOpen = false;
  if (active == FAULT_UV) { active = FAULT_NONE; locked = false; }
  eventLogAdd("reset_uv");
  saveNvs();
}

void faultsResetPrefilterVolume() {
  prefilterLiters = 0;
  if (active == FAULT_PREFILTER) { active = FAULT_NONE; locked = false; }
  eventLogAdd("reset_prefilter");
  saveNvs();
}

void faultsResetMembraneTest() {
  memTestActive = false;
  memHighSteps = 0;
  memLitersSinceStep = 0;
  memAvgActive = false;
  if (active == FAULT_MEMBRANE) { active = FAULT_NONE; locked = false; }
  eventLogAdd("reset_membrane");
  saveNvs();
}

void faultsTechnicianReset() {
  leakPhase = LEAK_PHASE_CLEAR;
  leakActiveSinceMs = 0;
  leakWaitUntilMs = 0;
  leakCountTotal = 0;
  clearLeakEpochs();
  dryTiming = false;
  dryRetries = 0;
  inDryWait = false;
  active = FAULT_NONE;
  locked = false;
  eventLogAdd("technician_reset");
  saveNvs();
}

void faultsResetLeakWait() {
  if (leakPhase != LEAK_PHASE_WAIT) {
    leakWaitUntilMs = 0;
    return;
  }
  // Zero countdown immediately, then end wait (hard-lock check or recover)
  leakWaitUntilMs = millis();
  finishLeakWait(true);
}
