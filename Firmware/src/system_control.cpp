#include "system_control.h"
#include "relay_control.h"
#include "scenario.h"
#include "event_log.h"
#include "intake.h"
#include "purify.h"
#include "faults.h"

static bool systemEnabled = false;

static void safeIdleActuators() {
  purificationOff();
  nightLightOff();
  relay2Off();
  if (scenarioIsA()) relay1On();
  else relay1Off();
}

void systemControlInit() {
  systemEnabled = false;
  eventLogInit();
  faultsInit();
  intakeInit();
  purifyInit();
  safeIdleActuators();
}

void systemControlSetEnabled(bool on) {
  if (faultsIsLocked() && on) return;
  systemEnabled = on;
  if (!on) {
    safeIdleActuators();
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

void systemControlUpdate(float /*tds1Ppm*/, bool /*tds1Valid*/,
                         float /*tds2Ppm*/, bool /*tds2Valid*/) {
  // Sensors already published via app_state before this call.
  faultsUpdate(systemEnabled);
  intakeUpdate(systemEnabled);
  purifyUpdate(systemEnabled);

  // Hard interlock Scenario B: never both motors
  if (scenarioIsB() && relay1IsOn() && purificationIsOn()) {
    purificationOff();
  }
}

ActiveRoutine systemControlRoutine() {
  if (faultsIsLocked()) return ROUTINE_LOCKED;
  if (!systemEnabled) return ROUTINE_IDLE;
  if (faultsInDryRunWait()) return ROUTINE_DRY_RUN_WAIT;
  if (purificationIsOn()) return ROUTINE_PURIFYING;
  switch (intakePhase()) {
    case INTAKE_NORMAL:
    case INTAKE_WAIT_30M:
    case INTAKE_FLUSH:
    case INTAKE_STANDBY_SOLAR:
      return ROUTINE_INTAKE;
    default:
      return ROUTINE_IDLE;
  }
}

FaultId systemControlFault() { return faultsActive(); }
bool systemControlIsLocked() { return faultsIsLocked(); }
uint8_t systemControlDryRunRetries() { return faultsDryRunRetries(); }
