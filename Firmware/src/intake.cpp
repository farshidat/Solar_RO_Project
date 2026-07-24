#include "intake.h"
#include "config.h"
#include "relay_control.h"
#include "scenario.h"
#include "plant_power.h"
#include "event_log.h"
#include "digital_inputs.h"
#include "faults.h"

static IntakePhase phase = INTAKE_PHASE_IDLE;
static uint32_t phaseSince = 0;
static uint32_t flowGoodSince = 0;
static bool flowTiming = false;

void intakeInit() {
  phase = INTAKE_PHASE_IDLE;
  phaseSince = millis();
  flowTiming = false;
}

IntakePhase intakePhase() { return phase; }

bool intakeBlocksPurify() {
  return phase == INTAKE_PHASE_WAIT_30M || phase == INTAKE_PHASE_FLUSH;
}

const char *intakePhaseName() {
  switch (phase) {
    case INTAKE_PHASE_NORMAL: return "intake_normal";
    case INTAKE_PHASE_WAIT_30M: return "intake_wait";
    case INTAKE_PHASE_FLUSH: return "intake_flush";
    case INTAKE_PHASE_STANDBY_SOLAR: return "intake_standby_solar";
    default: return "intake_idle";
  }
}

static void enter(IntakePhase p) {
  phase = p;
  phaseSince = millis();
  flowTiming = false;
}

static bool flowingNow() {
  if (scenarioIsA()) {
    // Inlet open and not waiting closed
    return !relay1IsOn() && phase != INTAKE_PHASE_WAIT_30M;
  }
  return relay1IsOn();
}

static bool tdsHighConfirmed(float tds1Ppm, bool tds1Valid, uint32_t now) {
  if (!tds1Valid || !flowingNow()) {
    flowTiming = false;
    return false;
  }
  if (tds1Ppm <= TDS1_LIMIT_PPM) {
    flowTiming = false;
    return false;
  }
  if (!flowTiming) {
    flowTiming = true;
    flowGoodSince = now;
    return false;
  }
  return (now - flowGoodSince) >= TDS_FLOW_VERIFY_MS;
}

static void updateScenarioA(uint32_t now, float tds1Ppm, bool tds1Valid) {
  switch (phase) {
    case INTAKE_PHASE_WAIT_30M:
      relay1On();   // inlet closed
      relay2Off();
      purificationOff();
      if ((now - phaseSince) >= INTAKE_WAIT_MS) enter(INTAKE_PHASE_FLUSH);
      break;

    case INTAKE_PHASE_FLUSH:
      // Both open for flush flow
      relay1Off();  // inlet open
      relay2On();   // drain open
      purificationOff();
      if ((now - phaseSince) >= INTAKE_FLUSH_MS_A) {
        if (tds1Valid && tds1Ppm > TDS1_LIMIT_PPM) {
          eventLogAdd("intake_A_still_dirty");
          enter(INTAKE_PHASE_WAIT_30M);
        } else {
          eventLogAdd("intake_A_clean");
          relay1Off();
          relay2Off();
          enter(INTAKE_PHASE_NORMAL);
        }
      }
      break;

    default:
      // NORMAL
      relay1Off();  // inlet open
      relay2Off();
      phase = INTAKE_PHASE_NORMAL;
      if (tdsHighConfirmed(tds1Ppm, tds1Valid, now)) {
        relay1On();
        relay2Off();
        eventLogAdd("intake_A_tds_high");
        enter(INTAKE_PHASE_WAIT_30M);
      }
      break;
  }
}

static void updateScenarioB(uint32_t now, float tds1Ppm, bool tds1Valid) {
  // Hard interlock: never purify while we command Relay1
  switch (phase) {
    case INTAKE_PHASE_STANDBY_SOLAR:
      relay1Off();
      relay2Off();
      purificationOff();
      if (plantSolarAbove(V_PUMP_START)) enter(INTAKE_PHASE_NORMAL);
      break;

    case INTAKE_PHASE_WAIT_30M:
      relay1Off();
      relay2On();   // keep drain path available / tank dump
      purificationOff();
      if ((now - phaseSince) >= INTAKE_WAIT_MS) enter(INTAKE_PHASE_FLUSH);
      break;

    case INTAKE_PHASE_FLUSH:
      // Pump ON + drain ON
      purificationOff();
      relay2On();
      if (!plantSolarAbove(V_PUMP_START)) {
        relay1Off();
        break;
      }
      relay1On();
      if ((now - phaseSince) >= INTAKE_FLUSH_MS_B) {
        if (tds1Valid && tds1Ppm > TDS1_LIMIT_PPM) {
          eventLogAdd("intake_B_still_dirty");
          enter(INTAKE_PHASE_WAIT_30M);
        } else {
          eventLogAdd("intake_B_clean");
          relay2Off();
          enter(INTAKE_PHASE_NORMAL);
        }
      }
      break;

    default:
      // Normal B: run raw pump only while pressure is low (fill 40L tank).
      // When pressure OK, stop Relay1 so purification can run (motor interlock).
      if (!plantSolarAbove(V_PUMP_START)) {
        relay1Off();
        relay2Off();
        enter(INTAKE_PHASE_STANDBY_SOLAR);
        break;
      }

      phase = INTAKE_PHASE_NORMAL;
      if (!digitalInputsGet().pressureOk) {
        purificationOff();
        relay1On();
        relay2Off();
        if (tdsHighConfirmed(tds1Ppm, tds1Valid, now)) {
          relay1Off();
          relay2On();
          eventLogAdd("intake_B_tds_high");
          enter(INTAKE_PHASE_WAIT_30M);
        }
      } else {
        relay1Off();
        relay2Off();
      }
      break;
  }
}

void intakeUpdate(bool systemEnabled, bool faultsLocked, float tds1Ppm, bool tds1Valid) {
  if (faultsLocked || faultsInDryRunWait() || !systemEnabled) {
    // Leave leak/fault actuator ownership to faults; just idle phase
    if (phase != INTAKE_PHASE_IDLE && (faultsLocked || !systemEnabled)) {
      enter(INTAKE_PHASE_IDLE);
    }
    if (faultsLocked || !systemEnabled) return;
    if (faultsInDryRunWait()) {
      relay1Off();
      return;
    }
  }

  const uint32_t now = millis();
  if (scenarioIsA()) updateScenarioA(now, tds1Ppm, tds1Valid);
  else updateScenarioB(now, tds1Ppm, tds1Valid);
}
