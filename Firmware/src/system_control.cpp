#include "system_control.h"
#include "config.h"
#include "relay_control.h"
#include "scenario.h"
#include "digital_inputs.h"
#include "event_log.h"
#include "intake.h"
#include "purify.h"
#include "faults.h"

static bool systemEnabled = false;

void systemControlInit() {
  systemEnabled = false;
  eventLogInit();
  faultsInit();
  intakeInit();
  purifyInit();
  // Safe idle actuators until user enables
  purificationOff();
  nightLightOff();
  relay2Off();
  if (scenarioIsA()) relay1On();
  else relay1Off();
}

void systemControlSetEnabled(bool on) {
  if (faultsIsLocked() && on) return;
  systemEnabled = on;
  if (!on) {
    purificationOff();
    nightLightOff();
    relay2Off();
    if (scenarioIsA()) relay1On();
    else relay1Off();
    eventLogAdd("system_off");
  } else {
    purificationOff();
    nightLightOff();
    relay2Off();
    relay1Off();  // A: inlet open, B: pump off until intake runs
    eventLogAdd("system_on");
  }
}

bool systemControlIsEnabled() { return systemEnabled; }

void systemControlRequestPurification(bool on) { systemControlSetEnabled(on); }

void systemControlRequestRelay1(bool on) {
  if (faultsIsLocked() || !systemEnabled) return;
  if (scenarioIsB() && on) purificationOff();
  if (on) relay1On();
  else relay1Off();
}

void systemControlUpdate(float tds1Ppm, bool tds1Valid, float tds2Ppm, bool tds2Valid) {
  // 1) Faults (background) — highest priority
  faultsUpdate(systemEnabled, tds2Ppm, tds2Valid);

  // 2) Intake routine
  intakeUpdate(systemEnabled, faultsIsLocked(), tds1Ppm, tds1Valid);

  // 3) Purification routine (paused during dry-run wait too)
  const bool pausePurify = intakeBlocksPurify() || faultsInDryRunWait();
  purifyUpdate(systemEnabled, faultsIsLocked(), pausePurify);

  // 4) Hard interlock Scenario B
  if (scenarioIsB() && relay1IsOn() && purificationIsOn()) {
    purificationOff();
  }
}

ActiveRoutine systemControlRoutine() {
  if (faultsIsLocked()) return ROUTINE_LOCKED;
  if (!systemEnabled) return ROUTINE_IDLE;
  if (faultsInDryRunWait()) return ROUTINE_DRY_RUN_WAIT;
  if (purificationIsOn()) return ROUTINE_PURIFYING;
  if (intakePhase() == INTAKE_PHASE_NORMAL ||
      intakePhase() == INTAKE_PHASE_WAIT_30M ||
      intakePhase() == INTAKE_PHASE_FLUSH ||
      intakePhase() == INTAKE_PHASE_STANDBY_SOLAR) {
    return ROUTINE_INTAKE;
  }
  return ROUTINE_IDLE;
}

FaultId systemControlFault() { return faultsActive(); }
bool systemControlIsLocked() { return faultsIsLocked(); }
uint8_t systemControlDryRunRetries() { return faultsDryRunRetries(); }
