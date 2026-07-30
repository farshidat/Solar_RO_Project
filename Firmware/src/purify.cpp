#include "purify.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "faults.h"
#include "intake.h"
#include "event_log.h"

// Scenario A: pressure switch must stay LOW 5s to stop, HIGH 5s to start.

static uint32_t aPressSince = 0;
static bool aPressTiming = false;
static bool aPressWasOk = false;

void purifyInit() {
  aPressTiming = false;
  aPressWasOk = false;
}

bool purifyIsRunning() { return purificationIsOn(); }

const char *purifyStateName() {
  return purificationIsOn() ? "purifying" : "idle";
}

static void resetAPressTimer() {
  aPressTiming = false;
}

// Returns true when `want` level has been stable for PURIFY_A_PRESSURE_CONFIRM_MS
static bool aPressureStable(bool wantOk, bool nowOk, uint32_t now) {
  if (nowOk != wantOk) {
    aPressTiming = false;
    return false;
  }
  if (!aPressTiming) {
    aPressTiming = true;
    aPressSince = now;
    return false;
  }
  return (now - aPressSince) >= PURIFY_A_PRESSURE_CONFIRM_MS;
}

void purifyUpdate(bool systemEnabled) {
  if (!scenarioIsConfigured() || !systemEnabled || faultsIsLocked() ||
      faultsInDryRunWait() || intakeBlocksPurify() || intakeRawWaitActive()) {
    purificationOff();
    resetAPressTimer();
    return;
  }

  const AppSensors s = appStateSensors();
  const uint32_t now = millis();
  const bool tankLow = !s.tankFull;

  if (scenarioIsB() && relay1IsOn()) {
    purificationOff();
    resetAPressTimer();
    return;
  }

  if (!tankLow) {
    purificationOff();
    resetAPressTimer();
    return;
  }

  if (purificationIsOn()) {
    if (!plantDayBandActive() || faultsIsLocked()) {
      purificationOff();
      resetAPressTimer();
      return;
    }
    if (scenarioIsB() && relay1IsOn()) {
      purificationOff();
      resetAPressTimer();
      return;
    }
    if (scenarioIsA()) {
      // Low pressure confirmed 5s → stop; brief dips ignored
      if (aPressureStable(false, s.pressureOk, now)) {
        purificationOff();
        resetAPressTimer();
        eventLogAdd("purify_A_pressure_low");
      }
      return;
    }
    if (scenarioIsB() && s.tankPressureBar < P_LOW_BAR) {
      purificationOff();
      return;
    }
    return;
  }

  // --- Start path ---
  bool ok = tankLow && plantDayBandActive() && !faultsIsLocked();
  if (scenarioIsA()) {
    // High pressure confirmed 5s → may start (any time after that confirmation)
    if (!ok || !aPressureStable(true, s.pressureOk, now)) return;
    purificationOn();
    resetAPressTimer();
    aPressWasOk = true;
    return;
  }

  ok = ok && (s.tankPressureBar >= P_LOW_BAR) && !relay1IsOn();
  if (ok) purificationOn();
}
