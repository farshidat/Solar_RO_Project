#include "purify.h"
#include "config.h"
#include "digital_inputs.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "faults.h"

void purifyInit() {}

bool purifyIsRunning() { return purificationIsOn(); }

const char *purifyStateName() {
  return purificationIsOn() ? "purifying" : "idle";
}

void purifyUpdate(bool systemEnabled, bool faultsLocked, bool intakeBlocked) {
  // Master off / hard lock / intake flush-wait: purification must yield
  if (!systemEnabled || faultsLocked || intakeBlocked) {
    purificationOff();
    return;
  }

  const DigitalInputState in = digitalInputsGet();
  const bool tankLow = !in.tankFull;
  const bool pressureOk = in.pressureOk;
  const bool solarStartOk = plantSolarAbove(V_SOLAR_START);
  const bool solarRunOk = plantSolarAbove(V_SOLAR_STOP);

  // Scenario B: two motors never together
  if (scenarioIsB() && relay1IsOn()) {
    purificationOff();
    return;
  }

  if (!tankLow) {
    purificationOff();
    return;
  }

  if (purificationIsOn()) {
    // Instant stop conditions (flowchart + brief), except low-pressure
    // which is owned by faults dry-run (Relay3 stays ON up to 30s).
    if (!solarRunOk || faultsIsLocked()) {
      purificationOff();
      return;
    }
    if (!pressureOk) {
      if (scenarioIsB()) relay1Off();
      return;  // keep Relay3 ON for dry-run sampling
    }
    return;  // keep running
  }

  // Start conditions
  bool canStart = tankLow && pressureOk && solarStartOk && !faultsIsLocked();
  if (scenarioIsB()) canStart = canStart && !relay1IsOn();

  if (canStart) purificationOn();
}
