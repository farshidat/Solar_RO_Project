#include "purify.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "faults.h"
#include "intake.h"

void purifyInit() {}

bool purifyIsRunning() { return purificationIsOn(); }

const char *purifyStateName() {
  return purificationIsOn() ? "purifying" : "idle";
}

void purifyUpdate(bool systemEnabled) {
  if (!systemEnabled || faultsIsLocked() || faultsInDryRunWait() ||
      intakeBlocksPurify() || intakeRawWaitActive()) {
    purificationOff();
    return;
  }

  const AppSensors s = appStateSensors();
  const bool tankLow = !s.tankFull;

  // Absolute priority: raw pump → purify off
  if (scenarioIsB() && relay1IsOn()) {
    purificationOff();
    return;
  }

  if (!tankLow) {
    purificationOff();
    return;
  }

  if (purificationIsOn()) {
    if (plantVSolar() < V_SOLAR_STOP || faultsIsLocked()) {
      purificationOff();
      return;
    }
    if (scenarioIsB() && relay1IsOn()) {
      purificationOff();
      return;
    }
    // A: pressure lost — keep R3 on so faults can count 30s dry-run
    if (scenarioIsA() && !s.pressureOk) return;
    // B: if pressure collapses below P_low, intake will take over next cycle
    if (scenarioIsB() && s.tankPressureBar < P_LOW_BAR) {
      purificationOff();
      return;
    }
    return;
  }

  bool ok = tankLow && plantSolarAbove(V_SOLAR_START) && !faultsIsLocked();
  if (scenarioIsA()) {
    ok = ok && s.pressureOk;
  } else {
    ok = ok && (s.tankPressureBar >= P_LOW_BAR) && !relay1IsOn();
  }
  if (ok) purificationOn();
}
