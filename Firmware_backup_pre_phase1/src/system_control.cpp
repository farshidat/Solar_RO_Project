#include "system_control.h"
#include "config.h"
#include "digital_inputs.h"
#include "relay_control.h"
#include "scenario.h"

static ActiveRoutine routine = ROUTINE_IDLE;
static SystemFault fault = FAULT_NONE;
static bool locked = false;

static uint32_t lowPressureSinceMs = 0;
static bool lowPressureTiming = false;
static uint8_t dryRunRetries = 0;
static uint32_t dryRunWaitUntilMs = 0;
static bool inDryRunWait = false;

static bool manualPurifyRequest = false;
static bool autoMode = true;  // Phase 1: automatic purification from sensors
static bool systemEnabled = true;

static void applySafeShutdown() {
  purificationOff();
  nightLightOff();
  relay2Off();
  if (scenarioIsA()) {
    relay1On();   // close inlet
  } else if (scenarioIsB()) {
    relay1Off();  // stop raw pump
  }
}

static void applyLeakActions() {
  applySafeShutdown();
}

static void enterLock(SystemFault f) {
  fault = f;
  locked = true;
  routine = ROUTINE_LOCKED;
  if (f == FAULT_LEAK) {
    applyLeakActions();
  } else {
    purificationOff();
    if (scenarioIsB()) relay1Off();
  }
  Serial.printf("SYSTEM LOCKED — fault=%u (needs physical reset)\n", (unsigned)f);
}

static bool solarAllowsPurify() {
#if PHASE1_IGNORE_VSOLAR
  // TODO Phase 2: require V_solar > V_start (and stop below V_stop)
  return true;
#else
  return false;
#endif
}

void systemControlInit() {
  routine = ROUTINE_IDLE;
  fault = FAULT_NONE;
  locked = false;
  dryRunRetries = 0;
  inDryRunWait = false;
  lowPressureTiming = false;
  manualPurifyRequest = false;
  autoMode = true;
  systemEnabled = true;
}

void systemControlSetEnabled(bool on) {
  if (locked && on) {
    Serial.println("System enable ignored — locked (physical reset required)");
    return;
  }
  systemEnabled = on;
  if (!on) {
    applySafeShutdown();
    lowPressureTiming = false;
    inDryRunWait = false;
    routine = ROUTINE_IDLE;
    Serial.println("System DISABLED (master OFF)");
  } else {
    autoMode = true;
    if (scenarioIsA()) relay1Off();  // reopen inlet for normal intake
    Serial.println("System ENABLED (master ON)");
  }
}

bool systemControlIsEnabled() { return systemEnabled; }

void systemControlRequestPurification(bool on) {
  if (locked || !systemEnabled) return;
  manualPurifyRequest = on;
  autoMode = false;
  if (!on) purificationOff();
}

void systemControlRequestRelay1(bool on) {
  if (locked || !systemEnabled) return;
  if (scenarioIsB() && on) {
    purificationOff();
  }
  if (on) relay1On();
  else relay1Off();
}

void systemControlUpdate() {
  DigitalInputState in = digitalInputsGet();
  uint32_t now = millis();

  // --- Fault 1: Leak (highest priority) ---
  if (in.leakDetected) {
    if (!locked || fault != FAULT_LEAK) {
      enterLock(FAULT_LEAK);
    } else {
      applyLeakActions();
    }
    return;
  }

  if (locked) {
    purificationOff();
    return;
  }

  // --- Master system OFF: hold safe shutdown, no auto routines ---
  if (!systemEnabled) {
    applySafeShutdown();
    routine = ROUTINE_IDLE;
    lowPressureTiming = false;
    return;
  }

  // --- Dry-run wait window after a fault attempt ---
  if (inDryRunWait) {
    routine = ROUTINE_DRY_RUN_WAIT;
    purificationOff();
    if (now < dryRunWaitUntilMs) {
      return;
    }
    inDryRunWait = false;
    Serial.println("Dry-run wait finished. Retrying purification logic.");
  }

  // Scenario B interlock: Relay1 active => purification must be off
  if (scenarioIsB() && relay1IsOn()) {
    purificationOff();
  }

  // --- Decide if purification should run ---
  bool wantPurify = false;
  if (autoMode) {
    wantPurify = !in.tankFull && in.pressureOk && solarAllowsPurify();
    if (scenarioIsB() && relay1IsOn()) wantPurify = false;
  } else {
    wantPurify = manualPurifyRequest && !in.tankFull && in.pressureOk && solarAllowsPurify();
    if (scenarioIsB() && relay1IsOn()) wantPurify = false;
  }

  // Instant stop (except low pressure — handled by 30s dry-run timer per brief §5.2)
  if (purificationIsOn()) {
    if (in.tankFull || (scenarioIsB() && relay1IsOn()) || !solarAllowsPurify()) {
      purificationOff();
      lowPressureTiming = false;
    }
  }

  if (wantPurify && !purificationIsOn()) {
    purificationOn();
  }

  if (purificationIsOn()) {
    routine = ROUTINE_PURIFYING;
  } else if (!inDryRunWait) {
    routine = ROUTINE_IDLE;
  }

  // --- Fault 2: Dry-run — Relay3 ON but pressure < 2 bar for > 30 s ---
  if (purificationIsOn() && !in.pressureOk) {
    if (!lowPressureTiming) {
      lowPressureTiming = true;
      lowPressureSinceMs = now;
    } else if ((now - lowPressureSinceMs) >= DRY_RUN_FAULT_MS) {
      purificationOff();
      if (scenarioIsB()) relay1Off();
      if (scenarioIsA()) relay1On();  // stop intake / close inlet

      dryRunRetries++;
      lowPressureTiming = false;
      Serial.printf("Dry-run fault attempt %u/%u\n", dryRunRetries, DRY_RUN_MAX_RETRIES);

      if (dryRunRetries >= DRY_RUN_MAX_RETRIES) {
        enterLock(FAULT_DRY_RUN);
        return;
      }

      inDryRunWait = true;
      dryRunWaitUntilMs = now + DRY_RUN_RETRY_WAIT_MS;
      routine = ROUTINE_DRY_RUN_WAIT;
      if (scenarioIsA()) relay1Off();  // reopen inlet before retry window ends
    }
  } else {
    lowPressureTiming = false;
    // Successful pressurized purify clears consecutive dry-run streak
    if (purificationIsOn() && in.pressureOk && dryRunRetries > 0) {
      dryRunRetries = 0;
    }
  }
}

ActiveRoutine systemControlRoutine() { return routine; }
SystemFault systemControlFault() { return fault; }
bool systemControlIsLocked() { return locked; }
uint8_t systemControlDryRunRetries() { return dryRunRetries; }
