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

// Night light from irradiance % (ON < 5%, OFF > 8%, short debounce)
static void updateNightLight(float irrPct, uint32_t now) {
  if (irrPct < IRR_NIGHT_LIGHT_ON_PCT) {
    brightTiming = false;
    if (!darkTiming) {
      darkTiming = true;
      darkSince = now;
    } else if (!nightLampLit && (now - darkSince) >= NIGHT_LIGHT_DEBOUNCE_MS) {
      nightLampLit = true;
    }
  } else if (irrPct > IRR_NIGHT_LIGHT_OFF_PCT) {
    darkTiming = false;
    if (!brightTiming) {
      brightTiming = true;
      brightSince = now;
    } else if (nightLampLit && (now - brightSince) >= NIGHT_LIGHT_DEBOUNCE_MS) {
      nightLampLit = false;
    }
  } else {
    // Between 5–8%: hold current lamp state; keep timers from fighting
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
  if (!scenarioIsConfigured() && on) return;  // first-run: pick Scenario A/B first
  systemEnabled = on;
  if (!on) {
    // Keep night-light logic running; only park water actuators
    purificationOff();
    relay2Off();
    if (scenarioIsA()) relay1On();
    else relay1Off();
  } else {
    purificationOff();
    relay2Off();
    relay1Off();
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
  if (scenarioIsB() && relay1IsOn()) return true;
  return false;
}

static void refreshOpMode(bool dayBand) {
  const AppSensors s = appStateSensors();

  if (!dayBand) {
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
  const float irrPct = plantIrradiancePct();
  const bool dayBand = plantDayBandActive();

  faultsUpdate(systemEnabled);

  // Night light is independent of waits / hard locks / master water OFF
  updateNightLight(irrPct, now);

  // First-run setup: no scenario yet — park water actuators, keep portal/UI alive
  if (!scenarioIsConfigured()) {
    purificationOff();
    relay2Off();
    relay1Off();
    systemEnabled = false;
    refreshOpMode(dayBand);
    return;
  }

  if (!systemEnabled || faultsIsLocked()) {
    // Park water path only; Relay4 left to updateNightLight above
    purificationOff();
    relay2Off();
    if (scenarioIsA()) relay1On();
    else relay1Off();
    refreshOpMode(dayBand);
    return;
  }

  if (!dayBand) {
    purificationOff();
    if (scenarioIsB()) relay1Off();
  }

  intakeUpdate(systemEnabled, dayBand);
  if (dayBand) purifyUpdate(systemEnabled);
  else {
    purificationOff();
    if (scenarioIsB()) relay1Off();
  }

  if (scenarioIsB() && relay1IsOn() && purificationIsOn()) {
    purificationOff();
  }

  refreshOpMode(dayBand);
}

OpMode systemControlOpMode() { return opMode; }
StandbyReason systemControlStandbyReason() { return standbyReason; }
bool systemControlNightLightOn() { return nightLampLit; }

const char *systemControlOpModeLabel() {
  static char buf[96];
  switch (opMode) {
    case STATE_ACTIVE:
      if (scenarioIsB() && relay1IsOn()) return "حالت فعال (آبگیری)";
      if (purificationIsOn()) return "حالت فعال (تصفیه)";
      return "حالت فعال";
    case STATE_STANDBY:
      switch (standbyReason) {
        case STANDBY_TANK_FULL: return "حالت انتظار (مخزن پر است)";
        case STANDBY_NO_RAW_WATER: return "حالت انتظار (عدم دسترسی به آب خام)";
        case STANDBY_FAULT: return "حالت انتظار (خطا / وقفه حفاظتی)";
        default: return "حالت انتظار";
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
