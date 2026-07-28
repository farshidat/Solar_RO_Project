#include "intake.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "event_log.h"
#include "faults.h"

static IntakePhase phase = INTAKE_IDLE;
static uint32_t phaseSince = 0;
static uint32_t flowSince = 0;
static bool flowTiming = false;

static uint32_t rawRunSince = 0;
static bool rawTiming = false;
static uint8_t rawFails = 0;
static uint32_t rawWaitUntil = 0;

void intakeInit() {
  phase = INTAKE_IDLE;
  phaseSince = millis();
  flowTiming = false;
  rawTiming = false;
  rawFails = 0;
}

IntakePhase intakePhase() { return phase; }

bool intakeBlocksPurify() {
  return phase == INTAKE_WAIT_30M || phase == INTAKE_FLUSH ||
         phase == INTAKE_RAW_DRY_WAIT;
}

bool intakeRawWaitActive() { return phase == INTAKE_RAW_DRY_WAIT; }

uint32_t intakeRawWaitRemainingMs() {
  if (phase != INTAKE_RAW_DRY_WAIT) return 0;
  uint32_t now = millis();
  if (now >= rawWaitUntil) return 0;
  return rawWaitUntil - now;
}

uint8_t intakeRawFailCount() { return rawFails; }

void intakeResetRawWait() {
  if (phase != INTAKE_RAW_DRY_WAIT || faultsIsLocked()) return;
  eventLogAdd("intake_raw_wait_reset");
  rawTiming = false;
  phase = INTAKE_NORMAL;
  phaseSince = millis();
}

const char *intakePhaseName() {
  switch (phase) {
    case INTAKE_NORMAL: return "intake_normal";
    case INTAKE_WAIT_30M: return "intake_wait";
    case INTAKE_FLUSH: return "intake_flush";
    case INTAKE_STANDBY_SOLAR: return "intake_standby_solar";
    case INTAKE_RAW_DRY_WAIT: return "intake_raw_dry_wait";
    default: return "intake_idle";
  }
}

static void enter(IntakePhase p) {
  phase = p;
  phaseSince = millis();
  flowTiming = false;
}

static bool isFlowing() {
  if (scenarioIsA()) return !relay1IsOn() && phase != INTAKE_WAIT_30M;
  return relay1IsOn();
}

static bool tdsHighConfirmed(const AppSensors &s, uint32_t now) {
  if (!s.tds1Valid || !isFlowing() || s.tds1Ppm <= TDS1_LIMIT_PPM) {
    flowTiming = false;
    return false;
  }
  if (!flowTiming) {
    flowTiming = true;
    flowSince = now;
    return false;
  }
  return (now - flowSince) >= TDS_FLOW_VERIFY_MS;
}

static void enterRawDryWait(uint32_t now) {
  purificationOff();
  relay1Off();
  relay2Off();
  rawTiming = false;
  rawFails++;
  char buf[40];
  snprintf(buf, sizeof(buf), "intake_raw_dry_%u", (unsigned)rawFails);
  eventLogAdd(buf);
  // No hard lock: keep cycling 5 min run → 30 min wait until P_high is reached
  rawWaitUntil = now + RAW_DRY_WAIT_MS;
  enter(INTAKE_RAW_DRY_WAIT);
}

static void runA(uint32_t now, const AppSensors &s) {
  switch (phase) {
    case INTAKE_WAIT_30M:
      purificationOff();
      relay1On();
      relay2Off();
      if ((now - phaseSince) >= INTAKE_WAIT_MS) enter(INTAKE_FLUSH);
      break;

    case INTAKE_FLUSH:
      purificationOff();
      relay1Off();
      relay2On();
      if ((now - phaseSince) >= INTAKE_FLUSH_MS_A) {
        if (s.tds1Valid && s.tds1Ppm > TDS1_LIMIT_PPM) {
          eventLogAdd("intake_A_still_dirty");
          enter(INTAKE_WAIT_30M);
        } else {
          eventLogAdd("intake_A_clean");
          relay1Off();
          relay2Off();
          enter(INTAKE_NORMAL);
        }
      }
      break;

    default:
      phase = INTAKE_NORMAL;
      relay1Off();
      relay2Off();
      if (tdsHighConfirmed(s, now)) {
        relay1On();
        relay2Off();
        eventLogAdd("intake_A_tds_high");
        enter(INTAKE_WAIT_30M);
      }
      break;
  }
}

static void runB(uint32_t now, const AppSensors &s, bool solarOk) {
  if (phase == INTAKE_RAW_DRY_WAIT) {
    purificationOff();
    relay1Off();
    if (now >= rawWaitUntil) {
      eventLogAdd("intake_raw_wait_done");
      enter(INTAKE_NORMAL);
    }
    return;
  }

  switch (phase) {
    case INTAKE_STANDBY_SOLAR:
      purificationOff();
      relay1Off();
      relay2Off();
      if (solarOk) enter(INTAKE_NORMAL);
      break;

    case INTAKE_WAIT_30M:
      purificationOff();
      relay1Off();
      relay2On();
      if ((now - phaseSince) >= INTAKE_WAIT_MS) enter(INTAKE_FLUSH);
      break;

    case INTAKE_FLUSH:
      purificationOff();
      relay2On();
      if (!solarOk) {
        relay1Off();
        break;
      }
      relay1On();
      if ((now - phaseSince) >= INTAKE_FLUSH_MS_B) {
        if (s.tds1Valid && s.tds1Ppm > TDS1_LIMIT_PPM) {
          eventLogAdd("intake_B_still_dirty");
          enter(INTAKE_WAIT_30M);
        } else {
          eventLogAdd("intake_B_clean");
          relay2Off();
          enter(INTAKE_NORMAL);
        }
      }
      break;

    default:
      if (!solarOk) {
        relay1Off();
        relay2Off();
        rawTiming = false;
        enter(INTAKE_STANDBY_SOLAR);
        break;
      }

      phase = INTAKE_NORMAL;
      {
        const float p = s.tankPressureBar;

        // Hysteresis: fill below P_low, stop above P_high
        if (p < P_LOW_BAR) {
          purificationOff();
          relay1On();
          relay2Off();
        } else if (p > P_HIGH_BAR) {
          relay1Off();
          rawTiming = false;
          rawFails = 0;  // successful fill clears consecutive dry streak
        }
        // between P_low and P_high: keep current R1 state (hysteresis band)

        if (relay1IsOn()) {
          if (!rawTiming) {
            rawTiming = true;
            rawRunSince = now;
          } else if ((now - rawRunSince) >= RAW_DRY_RUN_MS && p < P_HIGH_BAR) {
            enterRawDryWait(now);
            return;
          }
          if (tdsHighConfirmed(s, now)) {
            relay1Off();
            relay2On();
            rawTiming = false;
            eventLogAdd("intake_B_tds_high");
            enter(INTAKE_WAIT_30M);
          }
        } else {
          rawTiming = false;
          if (phase == INTAKE_NORMAL) relay2Off();
        }
      }
      break;
  }
}

void intakeUpdate(bool systemEnabled, bool solarOk) {
  if (!systemEnabled) {
    enter(INTAKE_IDLE);
    rawTiming = false;
    return;
  }
  if (faultsIsLocked()) {
    enter(INTAKE_IDLE);
    rawTiming = false;
    return;
  }

  if (faultsInDryRunWait()) {
    relay1Off();
    return;
  }

  // Preserve raw dry-wait countdown across night / low solar
  if (phase == INTAKE_RAW_DRY_WAIT) {
    const uint32_t now = millis();
    purificationOff();
    relay1Off();
    if (now >= rawWaitUntil) {
      eventLogAdd("intake_raw_wait_done");
      enter(INTAKE_NORMAL);
    }
    return;
  }

  if (!solarOk) {
    purificationOff();
    if (scenarioIsB()) {
      relay1Off();
      rawTiming = false;
      if (phase != INTAKE_WAIT_30M && phase != INTAKE_FLUSH)
        enter(INTAKE_STANDBY_SOLAR);
    }
    return;
  }

  const uint32_t now = millis();
  const AppSensors s = appStateSensors();
  if (scenarioIsA()) runA(now, s);
  else runB(now, s, true);
}
