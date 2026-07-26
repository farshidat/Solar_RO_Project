#include "system_control.h"
#include "config.h"
#include "relay_control.h"
#include "scenario.h"
#include "event_log.h"
#include "intake.h"
#include "purify.h"
#include "faults.h"
#include "app_state.h"
#include "plant_power.h"

static bool systemEnabled = false;
static OpMode opMode = STATE_NIGHT;
static StandbyReason standbyReason = STANDBY_NONE;
static bool nightLampLit = false;

static uint32_t darkSince = 0;
static bool darkTiming = false;
static uint32_t brightSince = 0;
static bool brightTiming = false;

static void safeIdleActuators(bool includeNight) {
  purificationOff();
  relay2Off();
  if (scenarioIsA()) relay1On();
  else relay1Off();
  if (includeNight) {
    nightLampLit = false;
    nightLightOff();
  }
}

static void updateNightLight(float vSolar, uint32_t now) {
  if (vSolar < NIGHT_LIGHT_ON_V) {
    brightTiming = false;
    if (!darkTiming) {
      darkTiming = true;
      darkSince = now;
    } else if (!nightLampLit && (now - darkSince) >= NIGHT_LIGHT_DEBOUNCE_MS) {
      nightLampLit = true;
    }
  } else if (vSolar > NIGHT_LIGHT_OFF_V) {
    darkTiming = false;
    if (!brightTiming) {
      brightTiming = true;
      brightSince = now;
    } else if (nightLampLit && (now - brightSince) >= NIGHT_LIGHT_DEBOUNCE_MS) {
      nightLampLit = false;
    }
  } else {
    darkTiming = false;
    brightTiming = false;
  }

  if (nightLampLit) nightLightOn();
  else nightLightOff();
}

void systemControlInit() {
  systemEnabled = false;
  opMode = STATE_NIGHT;
  standbyReason = STANDBY_NONE;
  nightLampLit = false;
  eventLogInit();
  faultsInit();
  intakeInit();
  purifyInit();
  safeIdleActuators(true);
}

void systemControlSetEnabled(bool on) {
  if (faultsIsLocked() && on) return;
  systemEnabled = on;
  if (!on) {
    safeIdleActuators(true);
    eventLogAdd("system_off");
  } else {
    purificationOff();
    nightLampLit = false;
    nightLightOff();
    relay2Off();
    relay1Off();
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

static bool isPumpMotorOn() {
  if (purificationIsOn()) return true;
  // Scenario B: Relay1 is raw pump. Scenario A: Relay1 is solenoid — not a pump.
  if (scenarioIsB() && relay1IsOn()) return true;
  return false;
}

static void refreshOpMode(float vSolar) {
  const AppSensors s = appStateSensors();

  if (vSolar < V_SOLAR_START) {
    opMode = STATE_NIGHT;
    standbyReason = STANDBY_NONE;
    return;
  }

  if (isPumpMotorOn()) {
    opMode = STATE_ACTIVE;
    standbyReason = STANDBY_NONE;
    return;
  }

  opMode = STATE_STANDBY;
  if (faultsIsLocked() || faultsInDryRunWait()) {
    standbyReason = STANDBY_FAULT;
  } else if (intakeRawWaitActive()) {
    standbyReason = STANDBY_NO_RAW_WATER;
  } else if (s.tankFull) {
    standbyReason = STANDBY_TANK_FULL;
  } else {
    standbyReason = STANDBY_OTHER;
  }
}

void systemControlUpdate() {
  const uint32_t now = millis();
  const float vSolar = plantVSolar();
  const bool solarOk = vSolar > V_SOLAR_START;

  // Leak / locks always
  faultsUpdate(systemEnabled);

  // Night light independent of master ON (battery lamps) — but off when hard-locked leak
  if (faultsIsLocked() && faultsActive() == FAULT_LEAK) {
    nightLampLit = false;
    nightLightOff();
  } else {
    updateNightLight(vSolar, now);
  }

  if (!systemEnabled || faultsIsLocked()) {
    if (systemEnabled == false) {
      // keep safe idle; night light may still run via updateNightLight above
      purificationOff();
      relay2Off();
      if (scenarioIsA()) relay1On();
      else relay1Off();
    }
    refreshOpMode(vSolar);
    return;
  }

  if (!solarOk) {
    // Night: stop solar-driven pumps; intake/purify idle
    purificationOff();
    if (scenarioIsB()) relay1Off();
    // A: leave inlet closed or open? Prefer closed for safety at night? Brief: pumps don't run.
    // Keep inlet open (Relay1 OFF) so municipal line not forced closed overnight — match prior safe day idle for A when enabled.
    if (scenarioIsA() && !intakeBlocksPurify()) {
      // if in TDS wait, intake still owns relays when we call intake — but solarOk false
    }
  }

  intakeUpdate(systemEnabled, solarOk);
  if (solarOk) purifyUpdate(systemEnabled);
  else {
    purificationOff();
    if (scenarioIsB()) relay1Off();
  }

  if (scenarioIsB() && relay1IsOn() && purificationIsOn()) {
    purificationOff();
  }

  refreshOpMode(vSolar);
}

OpMode systemControlOpMode() { return opMode; }
StandbyReason systemControlStandbyReason() { return standbyReason; }
bool systemControlNightLightOn() { return nightLampLit; }

const char *systemControlOpModeLabel() {
  static char buf[96];
  switch (opMode) {
    case STATE_ACTIVE:
      if (scenarioIsB() && relay1IsOn()) {
        return "حالت فعال (آبگیری)";
      }
      if (purificationIsOn()) {
        return "حالت فعال (تصفیه)";
      }
      return "حالت فعال";
    case STATE_STANDBY:
      switch (standbyReason) {
        case STANDBY_TANK_FULL:
          return "حالت انتظار (مخزن پر است)";
        case STANDBY_NO_RAW_WATER:
          return "حالت انتظار (عدم دسترسی به آب خام)";
        case STANDBY_FAULT:
          return "حالت انتظار (خطا / وقفه حفاظتی)";
        default:
          return "حالت انتظار";
      }
    case STATE_NIGHT:
    default:
      snprintf(buf, sizeof(buf), "حالت شب (چراغ شب: %s)",
               nightLampLit ? "روشن" : "خاموش");
      return buf;
  }
}

ActiveRoutine systemControlRoutine() {
  if (faultsIsLocked()) return ROUTINE_LOCKED;
  if (!systemEnabled) return ROUTINE_IDLE;
  if (faultsInDryRunWait() || intakeRawWaitActive()) return ROUTINE_DRY_RUN_WAIT;
  if (purificationIsOn()) return ROUTINE_PURIFYING;
  switch (intakePhase()) {
    case INTAKE_NORMAL:
    case INTAKE_WAIT_30M:
    case INTAKE_FLUSH:
    case INTAKE_STANDBY_SOLAR:
    case INTAKE_RAW_DRY_WAIT:
      return ROUTINE_INTAKE;
    default:
      return ROUTINE_IDLE;
  }
}

FaultId systemControlFault() { return faultsActive(); }
bool systemControlIsLocked() { return faultsIsLocked(); }
uint8_t systemControlDryRunRetries() { return faultsDryRunRetries(); }
