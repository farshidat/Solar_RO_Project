#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "tds_sensor.h"
#include "relay_control.h"
#include "web_server.h"
#include "digital_inputs.h"
#include "scenario.h"
#include "system_control.h"
#include "app_state.h"
#include "analog_bench.h"
#include "intake.h"
#include "purify.h"
#include "faults.h"
#include "event_log.h"

static const char *routineName(ActiveRoutine r) {
  switch (r) {
    case ROUTINE_INTAKE: return "intake";
    case ROUTINE_PURIFYING: return "purifying";
    case ROUTINE_DRY_RUN_WAIT: return "dry_run_wait";
    case ROUTINE_LOCKED: return "locked";
    default: return "idle";
  }
}

static const char *opModeCode(OpMode m) {
  switch (m) {
    case STATE_ACTIVE: return "active";
    case STATE_STANDBY: return "standby";
    default: return "night";
  }
}

static const char *standbyReasonCode(StandbyReason r) {
  switch (r) {
    case STANDBY_TANK_FULL: return "tank_full";
    case STANDBY_NO_RAW_WATER: return "no_raw_water";
    case STANDBY_FAULT: return "fault";
    case STANDBY_OTHER: return "other";
    default: return "none";
  }
}

static void sendCommandResult(const char *type, uint8_t channel, bool ok) {
  JsonDocument doc;
  doc["calibResult"]["type"] = type;
  doc["calibResult"]["channel"] = channel;
  doc["calibResult"]["ok"] = ok;
  String out;
  serializeJson(doc, out);
  webServerBroadcast(out);
}

static void handleWsCommand(JsonDocument &cmd) {
  const char *c = cmd["cmd"];
  if (!c) return;

  if (strcmp(c, "power") == 0 || strcmp(c, "system") == 0) {
    systemControlSetEnabled(cmd["on"].as<bool>());
  } else if (strcmp(c, "scenario") == 0) {
    const char *mode = cmd["mode"];
    if (mode && (mode[0] == 'A' || mode[0] == 'a')) scenarioSet(SCENARIO_A);
    else if (mode && (mode[0] == 'B' || mode[0] == 'b')) scenarioSet(SCENARIO_B);
  } else if (strcmp(c, "reset_intake_wait") == 0) {
    intakeResetRawWait();
  } else if (strcmp(c, "reset_uv") == 0) {
    faultsResetUvCounter();
  } else if (strcmp(c, "reset_prefilter") == 0) {
    faultsResetPrefilterVolume();
  } else if (strcmp(c, "reset_membrane") == 0) {
    faultsResetMembraneTest();
  } else if (strcmp(c, "calibrate_ec") == 0) {
    sendCommandResult("ec", (uint8_t)cmd["channel"],
                      tdsCalibrateConductivity(cmd["channel"], cmd["value"]));
  } else if (strcmp(c, "calibrate_temp") == 0) {
    sendCommandResult("temp", (uint8_t)cmd["channel"],
                      tdsCalibrateTemperature(cmd["channel"], cmd["value"]));
  }
}

static void broadcastStatus() {
  const AppSensors s = appStateSensors();
  const uint32_t waitMs = intakeRawWaitRemainingMs();

  JsonDocument doc;
  doc["scenario"] = scenarioName();
  doc["systemEnabled"] = systemControlIsEnabled();
  doc["opMode"] = opModeCode(systemControlOpMode());
  doc["opModeLabel"] = systemControlOpModeLabel();
  doc["standbyReason"] = standbyReasonCode(systemControlStandbyReason());
  doc["nightLight"] = systemControlNightLightOn();
  doc["routine"] = routineName(systemControlRoutine());
  doc["intakePhase"] = intakePhaseName();
  doc["purify"] = purifyStateName();
  doc["fault"] = faultsName(systemControlFault());
  doc["locked"] = systemControlIsLocked();
  doc["dryRunRetries"] = systemControlDryRunRetries();
  doc["intakeRawFails"] = intakeRawFailCount();
  doc["intakeWaitActive"] = intakeRawWaitActive();
  doc["intakeWaitMs"] = waitMs;
  doc["intakeWaitSec"] = (waitMs + 999) / 1000;
  doc["uvHours"] = faultsUvHours();
  doc["prefilterLiters"] = faultsPrefilterLiters();
  doc["membraneTest"] = faultsMembraneTestActive();
  doc["membraneStep"] = faultsMembraneTestStep();
  doc["vSolar"] = s.vSolar;
  doc["soc"] = s.socPercent;
  doc["irradiancePct"] = plantIrradiancePct();
  doc["dayBand"] = plantDayBandActive();
  // Top-level pressure (same pattern as vSolar) so UI always sees a number
  doc["tankPressureBar"] = s.tankPressureBar;
  doc["pressureAdc"] = benchPressureAdcVolts();
  doc["vSolarAdc"] = benchVSolarAdcVolts();
  doc["bench"]["enabled"] = (bool)BENCH_SIMULATION_MODE;
  doc["bench"]["vSolarAdc"] = benchVSolarAdcVolts();
  doc["bench"]["pressureAdc"] = benchPressureAdcVolts();
  doc["bench"]["tankPressureBar"] = s.tankPressureBar;
  doc["bench"]["vSolar"] = s.vSolar;
  doc["inputs"]["pressureOk"] = s.pressureOk;
  doc["inputs"]["tankFull"] = s.tankFull;
  doc["inputs"]["leak"] = s.leak;
  doc["inputs"]["tankPressureBar"] = s.tankPressureBar;
  doc["relays"]["r1"] = relay1IsOn();
  doc["relays"]["r2"] = relay2IsOn();
  doc["relays"]["purify"] = purificationIsOn();
  doc["relays"]["night"] = nightLightIsOn();
  doc["pumps"]["treatment"] = purificationIsOn();
  doc["pumps"]["uv"] = purificationIsOn();
  doc["pumps"]["raw"] = relay1IsOn();
  if (s.tds1Valid) {
    doc["tds1"]["ec"] = s.ec1;
    doc["tds1"]["temp"] = s.temp1C;
    doc["tds1"]["tds"] = s.tds1Ppm;
  }
  if (s.tds2Valid) {
    doc["tds2"]["ec"] = s.ec2;
    doc["tds2"]["temp"] = s.temp2C;
    doc["tds2"]["tds"] = s.tds2Ppm;
  }

  JsonArray logs = doc["events"].to<JsonArray>();
  uint8_t n = eventLogCount();
  if (n > 5) n = 5;
  for (uint8_t i = 0; i < n; i++) {
    EventLogEntry e = eventLogGet(i);
    JsonObject o = logs.add<JsonObject>();
    o["msg"] = e.msg;
    o["ms"] = e.millisStamp;
  }

  String out;
  serializeJson(doc, out);
  webServerBroadcast(out);
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(200);
  relayInit();
  digitalInputsInit();
  analogBenchInit();
  scenarioInit();
  appStateInit();
  systemControlInit();
  webServerInit();
  webServerOnCommand(handleWsCommand);
  tdsInit();
}

void loop() {
  digitalInputsUpdate();
  analogBenchUpdate();

  DigitalInputState in = digitalInputsGet();
  AppSensors s = {};
  s.pressureOk = in.pressureOk;
  s.tankFull = in.tankFull;
  s.leak = in.leakDetected;
  s.vSolar = plantVSolar();
  s.socPercent = plantSocPercent();
  s.tankPressureBar = tankPressureBar();
  s.tds1Valid = tdsRead(1, s.ec1, s.temp1C, s.tds1Ppm);
  s.tds2Valid = tdsRead(2, s.ec2, s.temp2C, s.tds2Ppm);
  appStateUpdateSensors(s);

  systemControlUpdate();
  broadcastStatus();
  delay(100);
}
