#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Telemetry: change detection + bounded JSON buffer (no String churn)
// ---------------------------------------------------------------------------

static bool gForceBroadcast = false;
static uint32_t gLastBroadcastMs = 0;
static uint32_t gLastTdsMs = 0;
static char gStatusBuf[STATUS_JSON_BUF_SIZE];

struct TelemetrySnap {
  uint8_t scenario;
  bool systemEnabled;
  uint8_t opMode;
  uint8_t standbyReason;
  bool nightLight;
  uint8_t routine;
  uint8_t fault;
  bool locked;
  bool intakeWaitActive;
  uint16_t intakeWaitSec;
  bool pressureOk;
  bool tankFull;
  bool leak;
  bool r1;
  bool r2;
  bool purify;
  bool night;
  int16_t vSolarX10;       // 0.1 V
  int16_t pressureX100;    // 0.01 bar
  int16_t irrX10;          // 0.1 %
  uint16_t eventGen;
};

static TelemetrySnap gLastSnap = {};
static bool gHaveSnap = false;

static AppSensors gLastTds = {};

static void requestBroadcast() { gForceBroadcast = true; }

static TelemetrySnap captureSnap(const AppSensors &s, uint32_t waitMs) {
  TelemetrySnap t = {};
  t.scenario = (uint8_t)(scenarioIsB() ? 1 : 0);
  t.systemEnabled = systemControlIsEnabled();
  t.opMode = (uint8_t)systemControlOpMode();
  t.standbyReason = (uint8_t)systemControlStandbyReason();
  t.nightLight = systemControlNightLightOn();
  t.routine = (uint8_t)systemControlRoutine();
  t.fault = (uint8_t)systemControlFault();
  t.locked = systemControlIsLocked();
  t.intakeWaitActive = intakeRawWaitActive();
  t.intakeWaitSec = (uint16_t)((waitMs + 999UL) / 1000UL);
  t.pressureOk = s.pressureOk;
  t.tankFull = s.tankFull;
  t.leak = s.leak;
  t.r1 = relay1IsOn();
  t.r2 = relay2IsOn();
  t.purify = purificationIsOn();
  t.night = nightLightIsOn();
  t.vSolarX10 = (int16_t)lroundf(s.vSolar * 10.0f);
  t.pressureX100 = (int16_t)lroundf(s.tankPressureBar * 100.0f);
  t.irrX10 = (int16_t)lroundf(plantIrradiancePct() * 10.0f);
  t.eventGen = eventLogGeneration();
  return t;
}

static bool snapChanged(const TelemetrySnap &a, const TelemetrySnap &b) {
  return memcmp(&a, &b, sizeof(TelemetrySnap)) != 0;
}

static void sendCommandResult(const char *type, uint8_t channel, bool ok) {
  JsonDocument doc;
  doc["calibResult"]["type"] = type;
  doc["calibResult"]["channel"] = channel;
  doc["calibResult"]["ok"] = ok;
  size_t n = serializeJson(doc, gStatusBuf, sizeof(gStatusBuf));
  if (n > 0 && n < sizeof(gStatusBuf)) webServerBroadcast(gStatusBuf, n);
}

static void handleWsCommand(JsonDocument &cmd) {
  const char *c = cmd["cmd"];
  if (!c) return;

  if (strcmp(c, "power") == 0 || strcmp(c, "system") == 0) {
    systemControlSetEnabled(cmd["on"].as<bool>());
    requestBroadcast();
  } else if (strcmp(c, "scenario") == 0) {
    const char *mode = cmd["mode"];
    if (mode && (mode[0] == 'A' || mode[0] == 'a')) scenarioSet(SCENARIO_A);
    else if (mode && (mode[0] == 'B' || mode[0] == 'b')) scenarioSet(SCENARIO_B);
    requestBroadcast();
  } else if (strcmp(c, "reset_intake_wait") == 0) {
    intakeResetRawWait();
    requestBroadcast();
  } else if (strcmp(c, "reset_uv") == 0) {
    faultsResetUvCounter();
    requestBroadcast();
  } else if (strcmp(c, "reset_prefilter") == 0) {
    faultsResetPrefilterVolume();
    requestBroadcast();
  } else if (strcmp(c, "reset_membrane") == 0) {
    faultsResetMembraneTest();
    requestBroadcast();
  } else if (strcmp(c, "calibrate_ec") == 0) {
    sendCommandResult("ec", (uint8_t)cmd["channel"],
                      tdsCalibrateConductivity(cmd["channel"], cmd["value"]));
  } else if (strcmp(c, "calibrate_temp") == 0) {
    sendCommandResult("temp", (uint8_t)cmd["channel"],
                      tdsCalibrateTemperature(cmd["channel"], cmd["value"]));
  }
}

/** Slim status JSON — no duplicate nest, events only when log generation changes. */
static void broadcastStatus(const AppSensors &s, uint32_t waitMs, bool includeEvents) {
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
  doc["intakeWaitSec"] = (waitMs + 999UL) / 1000UL;
  doc["vSolar"] = s.vSolar;
  doc["soc"] = s.socPercent;
  doc["irradiancePct"] = plantIrradiancePct();
  doc["dayBand"] = plantDayBandActive();
  doc["tankPressureBar"] = s.tankPressureBar;
  doc["pressureAdc"] = benchPressureAdcVolts();
  doc["vSolarAdc"] = benchVSolarAdcVolts();
  doc["bench"]["enabled"] = (bool)BENCH_SIMULATION_MODE;

  JsonObject inputs = doc["inputs"].to<JsonObject>();
  inputs["pressureOk"] = s.pressureOk;
  inputs["tankFull"] = s.tankFull;
  inputs["leak"] = s.leak;

  JsonObject relays = doc["relays"].to<JsonObject>();
  relays["r1"] = relay1IsOn();
  relays["r2"] = relay2IsOn();
  relays["purify"] = purificationIsOn();
  relays["night"] = nightLightIsOn();

  JsonObject pumps = doc["pumps"].to<JsonObject>();
  pumps["treatment"] = purificationIsOn();
  pumps["uv"] = purificationIsOn();
  pumps["raw"] = relay1IsOn();

  if (s.tds1Valid) {
    JsonObject t = doc["tds1"].to<JsonObject>();
    t["ec"] = s.ec1;
    t["temp"] = s.temp1C;
    t["tds"] = s.tds1Ppm;
  }
  if (s.tds2Valid) {
    JsonObject t = doc["tds2"].to<JsonObject>();
    t["ec"] = s.ec2;
    t["temp"] = s.temp2C;
    t["tds"] = s.tds2Ppm;
  }

  if (includeEvents) {
    JsonArray logs = doc["events"].to<JsonArray>();
    uint8_t n = eventLogCount();
    if (n > 5) n = 5;
    for (uint8_t i = 0; i < n; i++) {
      EventLogEntry e = eventLogGet(i);
      JsonObject o = logs.add<JsonObject>();
      o["msg"] = e.msg;
      o["ms"] = e.millisStamp;
    }
  }

  size_t n = serializeJson(doc, gStatusBuf, sizeof(gStatusBuf));
  if (n > 0 && n < sizeof(gStatusBuf)) {
    gStatusBuf[n] = '\0';
    webServerBroadcast(gStatusBuf, n);
  }
}

static void maybeBroadcast(const AppSensors &s) {
  if (!webServerHasClients()) {
    gHaveSnap = false;
    return;
  }

  const uint32_t now = millis();
  const uint32_t waitMs = intakeRawWaitRemainingMs();
  const TelemetrySnap snap = captureSnap(s, waitMs);
  const bool changed = !gHaveSnap || snapChanged(snap, gLastSnap);
  const bool due = (now - gLastBroadcastMs) >= STATUS_BROADCAST_MS;
  const bool heartbeat = (now - gLastBroadcastMs) >= STATUS_HEARTBEAT_MS;

  if (!(gForceBroadcast || (due && changed) || heartbeat)) return;

  const bool eventsDirty = !gHaveSnap || (snap.eventGen != gLastSnap.eventGen);
  broadcastStatus(s, waitMs, eventsDirty || gForceBroadcast);
  gLastSnap = snap;
  gHaveSnap = true;
  gLastBroadcastMs = now;
  gForceBroadcast = false;
}

// ---------------------------------------------------------------------------
// Arduino entry
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(200);  // USB enumerate only — not in loop
  relayInit();
  digitalInputsInit();
  analogBenchInit();
  scenarioInit();
  appStateInit();
  systemControlInit();
  webServerInit();
  webServerOnCommand(handleWsCommand);
  tdsInit();
  gLastBroadcastMs = 0;
  gLastTdsMs = 0;
}

void loop() {
  const uint32_t now = millis();

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

  // TDS UART can block tens of ms — poll slowly and reuse last sample
  if ((now - gLastTdsMs) >= TDS_POLL_MS) {
    gLastTdsMs = now;
    gLastTds.tds1Valid = tdsRead(1, gLastTds.ec1, gLastTds.temp1C, gLastTds.tds1Ppm);
    gLastTds.tds2Valid = tdsRead(2, gLastTds.ec2, gLastTds.temp2C, gLastTds.tds2Ppm);
  }
  s.tds1Valid = gLastTds.tds1Valid;
  s.ec1 = gLastTds.ec1;
  s.temp1C = gLastTds.temp1C;
  s.tds1Ppm = gLastTds.tds1Ppm;
  s.tds2Valid = gLastTds.tds2Valid;
  s.ec2 = gLastTds.ec2;
  s.temp2C = gLastTds.temp2C;
  s.tds2Ppm = gLastTds.tds2Ppm;

  appStateUpdateSensors(s);
  systemControlUpdate();
  maybeBroadcast(s);

  delay(LOOP_IDLE_DELAY_MS);
}
