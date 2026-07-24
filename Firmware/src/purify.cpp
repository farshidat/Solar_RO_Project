#include "purify.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "faults.h"
#include "intake.h"

// Purification — PROJECT_BRIEF §4.B / purify flowchart
// Low-pressure while Relay3 ON is sampled by faults (dry-run 30s).

void purifyInit() {}

bool purifyIsRunning() { return purificationIsOn(); }

const char *purifyStateName() {
  return purificationIsOn() ? "purifying" : "idle";
}

void purifyUpdate(bool systemEnabled) {
  if (!systemEnabled || faultsIsLocked() || faultsInDryRunWait() || intakeBlocksPurify()) {
    purificationOff();
    return;
  }

  const AppSensors s = appStateSensors();
  const bool tankLow = !s.tankFull;

  // B interlock: raw pump running ⇒ purify off
  if (scenarioIsB() && relay1IsOn()) {
    purificationOff();
    return;
  }

  // Float not low ⇒ stop / do not start
  if (!tankLow) {
    purificationOff();
    return;
  }

  if (purificationIsOn()) {
    // Instant stops
    if (!plantSolarAbove(V_SOLAR_STOP) || faultsIsLocked()) {
      purificationOff();
      return;
    }
    if (scenarioIsB() && relay1IsOn()) {
      purificationOff();
      return;
    }
    // Pressure lost: keep Relay3 ON so faults can count dry-run 30s
    if (!s.pressureOk) {
      if (scenarioIsB()) relay1Off();
      return;
    }
    return;
  }

  // Start conditions
  bool ok = tankLow && s.pressureOk && plantSolarAbove(V_SOLAR_START) && !faultsIsLocked();
  if (scenarioIsB()) ok = ok && !relay1IsOn();
  if (ok) purificationOn();
}
