#include "system_control.h"
#include "config.h"
#include "digital_inputs.h"
#include "relay_control.h"
#include "scenario.h"

static ActiveRoutine routine = ROUTINE_IDLE;
static SystemFault fault = FAULT_NONE;
static bool locked = false;
static bool systemEnabled = true;

static uint32_t lowPressureSinceMs = 0;
static bool lowPressureTiming = false;
static uint8_t dryRunRetries = 0;
static uint32_t dryRunWaitUntilMs = 0;
static bool inDryRunWait = false;

static void applySafeShutdown() {
  purificationOff();
  nightLightOff();
  relay2Off();
  if (scenarioIsA()) {
    relay1On();   // close inlet
  } else {
    relay1Off();  // stop raw pump
  }
}

static void enterLock(SystemFault f) {
  fault = f;
  locked = true;
  routine = ROUTINE_LOCKED;
  applySafeShutdown();
  Serial.printf("SYSTEM LOCKED — fault=%u (needs physical reset / power cycle)\n", (unsigned)f);
}

static bool solarOk() {
#if PHASE1_IGNORE_VSOLAR
  // TODO Phase 2: use real V_solar thresholds
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
  systemEnabled = true;
}

void systemControlSetEnabled(bool on) {
  if (locked && on) {
    Serial.println("System enable ignored — locked");
    return;
  }
  systemEnabled = on;
  if (!on) {
    applySafeShutdown();
    lowPressureTiming = false;
    inDryRunWait = false;
    routine = ROUTINE_IDLE;
    Serial.println("System DISABLED");
  } else {
    // Normal idle actuators for each scenario
    if (scenarioIsA()) relay1Off();  // inlet open
    else relay1Off();                // raw pump off until intake needs it
    purificationOff();
    Serial.println("System ENABLED");
  }
}

bool systemControlIsEnabled() { return systemEnabled; }

void systemControlRequestPurification(bool on) {
  // Master switch owns enable; keep API for compatibility
  systemControlSetEnabled(on);
}

void systemControlRequestRelay1(bool on) {
  if (locked || !systemEnabled) return;
  if (scenarioIsB() && on) purificationOff();
  if (on) relay1On();
  else relay1Off();
}

static void handleDryRunPressureProblem(uint32_t now) {
  // Count time while we need water but pressure is missing
  if (!lowPressureTiming) {
    lowPressureTiming = true;
    lowPressureSinceMs = now;
    return;
  }
  if ((now - lowPressureSinceMs) < DRY_RUN_FAULT_MS) return;

  purificationOff();
  if (scenarioIsB()) relay1Off();
  if (scenarioIsA()) relay1On();  // close inlet / stop intake

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
  if (scenarioIsA()) relay1Off();  // reopen inlet after closing for the fault action
}

static void clearDryRunTimerIfPressureOk(const DigitalInputState &in) {
  if (in.pressureOk) {
    lowPressureTiming = false;
    if (dryRunRetries > 0 && purificationIsOn()) {
      dryRunRetries = 0;
    }
  }
}

void systemControlUpdate() {
  DigitalInputState in = digitalInputsGet();
  uint32_t now = millis();

  // --- Leak always wins ---
  if (in.leakDetected) {
    if (!locked || fault != FAULT_LEAK) enterLock(FAULT_LEAK);
    else applySafeShutdown();
    return;
  }

  if (locked) {
    applySafeShutdown();
    return;
  }

  if (!systemEnabled) {
    applySafeShutdown();
    routine = ROUTINE_IDLE;
    lowPressureTiming = false;
    return;
  }

  if (!solarOk()) {
    purificationOff();
    if (scenarioIsB()) relay1Off();
    routine = ROUTINE_IDLE;
    return;
  }

  // --- Dry-run cooldown ---
  if (inDryRunWait) {
    purificationOff();
    if (scenarioIsB()) relay1Off();
    routine = ROUTINE_DRY_RUN_WAIT;
    if (now < dryRunWaitUntilMs) return;
    inDryRunWait = false;
    Serial.println("Dry-run wait finished — retrying.");
  }

  const bool needProduct = !in.tankFull;  // tank low → need production

  // ========== Scenario A: mains inlet ==========
  // Relay1 OFF = inlet open (normal). Relay1 ON = inlet closed.
  if (scenarioIsA()) {
    // Keep inlet open during normal operation
    if (!inDryRunWait) relay1Off();

    if (!needProduct) {
      purificationOff();
      routine = ROUTINE_IDLE;
      lowPressureTiming = false;
      return;
    }

    if (in.pressureOk) {
      purificationOn();
      routine = ROUTINE_PURIFYING;
      clearDryRunTimerIfPressureOk(in);
    } else {
      // Brief: stop purification immediately when pressure lost
      purificationOff();
      routine = ROUTINE_IDLE;
      handleDryRunPressureProblem(now);
    }
    return;
  }

  // ========== Scenario B: raw pump + pressure tank ==========
  // Interlock: never run Relay1 and Relay3 together.
  if (!needProduct) {
    purificationOff();
    relay1Off();
    routine = ROUTINE_IDLE;
    lowPressureTiming = false;
    return;
  }

  if (!in.pressureOk) {
    // Need pressure first → run raw pump (intake), purification forced off.
    // Dry-run fault applies only while Relay3 is active (brief §5.2), not during intake.
    purificationOff();
    relay1On();
    routine = ROUTINE_INTAKE;
    lowPressureTiming = false;
    return;
  }

  // Pressure OK → stop raw pump, then purify
  relay1Off();
  purificationOn();
  routine = ROUTINE_PURIFYING;
  clearDryRunTimerIfPressureOk(in);
}

ActiveRoutine systemControlRoutine() { return routine; }
SystemFault systemControlFault() { return fault; }
bool systemControlIsLocked() { return locked; }
uint8_t systemControlDryRunRetries() { return dryRunRetries; }
