#include "system_control.h"
#include "config.h"
#include "digital_inputs.h"
#include "relay_control.h"
#include "scenario.h"

// Phase-1 engine from PROJECT_BRIEF §§4–5
// Later phases: V_solar, TDS intake flush, night light, UV/filter hours

static ActiveRoutine routine = ROUTINE_IDLE;
static SystemFault fault = FAULT_NONE;
static bool locked = false;
static bool systemEnabled = false;  // OFF until Web master switch

static uint32_t dryLowPSinceMs = 0;
static bool dryLowPTiming = false;
static uint8_t dryRunRetries = 0;
static uint32_t dryRunWaitUntilMs = 0;
static bool inDryRunWait = false;

static void shutdownSafe() {
  purificationOff();
  nightLightOff();
  relay2Off();
  if (scenarioIsA()) relay1On();   // close inlet
  else relay1Off();                // stop raw pump
}

static void enterLock(SystemFault f) {
  fault = f;
  locked = true;
  routine = ROUTINE_LOCKED;
  shutdownSafe();
}

static bool solarOk() {
#if PHASE1_IGNORE_VSOLAR
  return true;  // TODO Phase 2
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
  dryLowPTiming = false;
  systemEnabled = false;
  shutdownSafe();
}

void systemControlSetEnabled(bool on) {
  if (locked && on) return;

  systemEnabled = on;
  if (!on) {
    shutdownSafe();
    dryLowPTiming = false;
    inDryRunWait = false;
    routine = ROUTINE_IDLE;
    return;
  }

  purificationOff();
  nightLightOff();
  relay2Off();
  // A: inlet open (Relay1 OFF). B: raw pump OFF until intake needs it.
  relay1Off();
  dryLowPTiming = false;
}

bool systemControlIsEnabled() { return systemEnabled; }

void systemControlRequestPurification(bool on) { systemControlSetEnabled(on); }

void systemControlRequestRelay1(bool on) {
  if (locked || !systemEnabled) return;
  if (scenarioIsB() && on) purificationOff();
  if (on) relay1On();
  else relay1Off();
}

static void beginDryRunWait(uint32_t now) {
  purificationOff();
  if (scenarioIsA()) relay1On();
  else relay1Off();

  dryRunRetries++;
  dryLowPTiming = false;

  if (dryRunRetries >= DRY_RUN_MAX_RETRIES) {
    enterLock(FAULT_DRY_RUN);
    return;
  }

  inDryRunWait = true;
  dryRunWaitUntilMs = now + DRY_RUN_RETRY_WAIT_MS;
  routine = ROUTINE_DRY_RUN_WAIT;
  if (scenarioIsA()) relay1Off();  // reopen inlet after stop-intake pulse
}

// §5.2: Relay3 active + pressure low for > 30 s
static void updateDryRunWhilePurifying(bool pressureOk, uint32_t now) {
  if (!purificationIsOn()) {
    dryLowPTiming = false;
    return;
  }
  if (pressureOk) {
    dryLowPTiming = false;
    if (dryRunRetries > 0) dryRunRetries = 0;
    return;
  }
  // keep Relay1 off (B interlock) while purifying
  if (scenarioIsB()) relay1Off();

  if (!dryLowPTiming) {
    dryLowPTiming = true;
    dryLowPSinceMs = now;
  } else if ((now - dryLowPSinceMs) >= DRY_RUN_FAULT_MS) {
    beginDryRunWait(now);
  }
}

void systemControlUpdate() {
  const DigitalInputState in = digitalInputsGet();
  const uint32_t now = millis();

  // §5.1 Leak
  if (in.leakDetected) {
    if (!locked || fault != FAULT_LEAK) enterLock(FAULT_LEAK);
    else shutdownSafe();
    return;
  }

  if (locked) {
    shutdownSafe();
    routine = ROUTINE_LOCKED;
    return;
  }

  if (!systemEnabled) {
    shutdownSafe();
    routine = ROUTINE_IDLE;
    dryLowPTiming = false;
    return;
  }

  if (!solarOk()) {
    purificationOff();
    if (scenarioIsB()) relay1Off();
    routine = ROUTINE_IDLE;
    return;
  }

  if (inDryRunWait) {
    purificationOff();
    if (scenarioIsB()) relay1Off();
    routine = ROUTINE_DRY_RUN_WAIT;
    if (now < dryRunWaitUntilMs) return;
    inDryRunWait = false;
  }

  const bool tankLow = !in.tankFull;

  // ----- Scenario A: mains (Relay1 OFF = inlet open) -----
  if (scenarioIsA()) {
    relay1Off();

    if (!tankLow) {
      purificationOff();
      routine = ROUTINE_IDLE;
      dryLowPTiming = false;
      return;
    }

    // §4.B start: tank low + pressure OK
    if (in.pressureOk) {
      purificationOn();
      routine = ROUTINE_PURIFYING;
    } else if (purificationIsOn()) {
      // pressure lost while purifying → §5.2 timing (Relay3 stays on until 30s)
      routine = ROUTINE_PURIFYING;
    } else {
      purificationOff();
      routine = ROUTINE_IDLE;
    }

    updateDryRunWhilePurifying(in.pressureOk, now);
    return;
  }

  // ----- Scenario B: raw pump + 40L (never R1+R3 together) -----
  if (!tankLow) {
    purificationOff();
    relay1Off();
    routine = ROUTINE_IDLE;
    dryLowPTiming = false;
    return;
  }

  // If currently purifying, allow §5.2 dry-run path on pressure loss
  if (purificationIsOn() && !in.pressureOk) {
    relay1Off();
    routine = ROUTINE_PURIFYING;
    updateDryRunWhilePurifying(false, now);
    return;
  }

  if (!in.pressureOk) {
    // Intake: build pressure (§4.A B) — Phase1 ignores V_solar
    purificationOff();
    relay1On();
    routine = ROUTINE_INTAKE;
    dryLowPTiming = false;
    return;
  }

  // Pressure OK → R1 off, purify (§4.B start + B interlock)
  relay1Off();
  purificationOn();
  routine = ROUTINE_PURIFYING;
  updateDryRunWhilePurifying(true, now);
}

ActiveRoutine systemControlRoutine() { return routine; }
SystemFault systemControlFault() { return fault; }
bool systemControlIsLocked() { return locked; }
uint8_t systemControlDryRunRetries() { return dryRunRetries; }
