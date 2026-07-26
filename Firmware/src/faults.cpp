#include "faults.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "event_log.h"
#include <Preferences.h>

static FaultId active = FAULT_NONE;
static bool locked = false;

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

void faultsForceLock(FaultId id, const char *logMsg) {
  active = id;
  locked = true;
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
  prefs.end();
}

static void saveNvs() {
  prefs.begin("solar_ro", false);
  prefs.putULong64("uv_ms", uvOnMsTotal);
  prefs.putFloat("filt_l", prefilterLiters);
  prefs.putBool("mem_on", memTestActive);
  prefs.putUChar("mem_st", memHighSteps);
  prefs.end();
  lastNvsSaveMs = millis();
}

void faultsInit() {
  active = FAULT_NONE;
  locked = false;
  dryTiming = false;
  dryRetries = 0;
  inDryWait = false;
  uvSegmentOpen = false;
  memAvgActive = false;
  lastR3SampleMs = millis();
  loadNvs();
  lastNvsSaveMs = millis();
}

const char *faultsName(FaultId id) {
  switch (id) {
    case FAULT_LEAK: return "leak";
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

// Scenario A only: Relay3 ON + pressure switch open > 30s
static void updatePurifyDryRunA(uint32_t now, bool pressureOk) {
  if (!scenarioIsA()) {
    dryTiming = false;
    return;
  }

  if (inDryWait) {
    purificationOff();
    if (now >= dryWaitUntil) {
      inDryWait = false;
      eventLogAdd("dry_run_retry");
    }
    return;
  }

  if (purificationIsOn() && !pressureOk) {
    if (!dryTiming) {
      dryTiming = true;
      dryLowSince = now;
    } else if ((now - dryLowSince) >= DRY_RUN_FAULT_MS) {
      purificationOff();
      relay1On();
      relay2Off();
      dryTiming = false;
      dryRetries++;
      char buf[40];
      snprintf(buf, sizeof(buf), "dry_run_%u", (unsigned)dryRetries);
      eventLogAdd(buf);
      if (dryRetries >= DRY_RUN_MAX_RETRIES) {
        lockFault(FAULT_DRY_RUN, "lock_dry_run");
        return;
      }
      inDryWait = true;
      dryWaitUntil = now + DRY_RUN_RETRY_WAIT_MS;
    }
  } else {
    dryTiming = false;
    if (purificationIsOn() && pressureOk) dryRetries = 0;
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

void faultsUpdate(bool systemEnabled) {
  const AppSensors s = appStateSensors();
  const uint32_t now = millis();

  accumulateRuntime(now);

  if (s.leak) {
    if (!locked || active != FAULT_LEAK) {
      lockFault(FAULT_LEAK, "lock_leak");
    } else {
      actuatorsSafeShutdown();
    }
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
  // Legacy 30s / 15m dry-run lock path is not used.
  (void)now;
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
