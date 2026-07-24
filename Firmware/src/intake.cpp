#include "intake.h"
#include "config.h"
#include "app_state.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "event_log.h"
#include "faults.h"

// Water intake — PROJECT_BRIEF §4.A / intake flowchart

static IntakePhase phase = INTAKE_IDLE;
static uint32_t phaseSince = 0;
static uint32_t flowSince = 0;
static bool flowTiming = false;

void intakeInit() {
  phase = INTAKE_IDLE;
  phaseSince = millis();
  flowTiming = false;
}

IntakePhase intakePhase() { return phase; }

bool intakeBlocksPurify() {
  return phase == INTAKE_WAIT_30M || phase == INTAKE_FLUSH;
}

const char *intakePhaseName() {
  switch (phase) {
    case INTAKE_NORMAL: return "intake_normal";
    case INTAKE_WAIT_30M: return "intake_wait";
    case INTAKE_FLUSH: return "intake_flush";
    case INTAKE_STANDBY_SOLAR: return "intake_standby_solar";
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

static void runA(uint32_t now, const AppSensors &s) {
  switch (phase) {
    case INTAKE_WAIT_30M:
      purificationOff();
      relay1On();   // close inlet
      relay2Off();
      if ((now - phaseSince) >= INTAKE_WAIT_MS) enter(INTAKE_FLUSH);
      break;

    case INTAKE_FLUSH:
      // Flush needs BOTH inlet open and drain open
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
      relay1Off();  // inlet open
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

static void runB(uint32_t now, const AppSensors &s) {
  switch (phase) {
    case INTAKE_STANDBY_SOLAR:
      purificationOff();
      relay1Off();
      relay2Off();
      if (plantSolarAbove(V_PUMP_START)) enter(INTAKE_NORMAL);
      break;

    case INTAKE_WAIT_30M:
      purificationOff();
      relay1Off();
      relay2On();  // dump / drain tank
      if ((now - phaseSince) >= INTAKE_WAIT_MS) enter(INTAKE_FLUSH);
      break;

    case INTAKE_FLUSH:
      // Flush: pump ON + drain ON (both path open)
      purificationOff();
      relay2On();
      if (!plantSolarAbove(V_PUMP_START)) {
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
      if (!plantSolarAbove(V_PUMP_START)) {
        relay1Off();
        relay2Off();
        enter(INTAKE_STANDBY_SOLAR);
        break;
      }

      phase = INTAKE_NORMAL;
      // Fill pressure tank only while pressure low; stop R1 when OK so purify can run
      if (!s.pressureOk) {
        purificationOff();  // B motor interlock
        relay1On();
        relay2Off();
        if (tdsHighConfirmed(s, now)) {
          relay1Off();
          relay2On();
          eventLogAdd("intake_B_tds_high");
          enter(INTAKE_WAIT_30M);
        }
      } else {
        relay1Off();
        relay2Off();
      }
      break;
  }
}

void intakeUpdate(bool systemEnabled) {
  if (!systemEnabled || faultsIsLocked()) {
    enter(INTAKE_IDLE);
    return;
  }

  if (faultsInDryRunWait()) {
    relay1Off();
    return;
  }

  const uint32_t now = millis();
  const AppSensors s = appStateSensors();
  if (scenarioIsA()) runA(now, s);
  else runB(now, s);
}
