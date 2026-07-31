#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
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
#include "event_codes.h"
#include "modbus_tracer.h"

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

/** Spec mode: 1 Active, 2 Standby, 3 Night */
static uint8_t opModeNumber(OpMode m) {
  switch (m) {
    case STATE_ACTIVE: return 1;
    case STATE_STANDBY: return 2;
    default: return 3;
  }
}

static const char *subModeString() {
  if (faultsLeakPhase() == LEAK_PHASE_HARD_LOCK) return "HardLock";
  if (faultsLeakPhase() == LEAK_PHASE_ACTIVE) return "LeakActive";
  if (faultsLeakPhase() == LEAK_PHASE_WAIT) return "LeakDryOut";
  if (intakeRawWaitActive()) return "RawPumpWait";
  if (faultsActive() == FAULT_UV) return "UvLock";
  if (faultsActive() == FAULT_PREFILTER) return "PrefilterLock";
  if (faultsActive() == FAULT_MEMBRANE) return "MembraneLock";
  if (purificationIsOn()) return "Purification";
  if (scenarioIsB() && relay1IsOn()) return "Intake";
  if (systemControlOpMode() == STATE_NIGHT) return "Night";
  if (systemControlOpMode() == STATE_STANDBY) return "Standby";
  return "Idle";
}

static const char *resolveActiveCode() {
  const char *c = faultsActiveCode();
  if (c && c[0]) return c;
  if (intakeRawWaitActive()) return CODE_O302;
  return "";
}

static void formatTimerMmSs(uint32_t ms, char *out, size_t outLen) {
  uint32_t sec = (ms + 999UL) / 1000UL;
  uint32_t mm = sec / 60UL;
  uint32_t ss = sec % 60UL;
  snprintf(out, outLen, "%02lu:%02lu", (unsigned long)mm, (unsigned long)ss);
}

static void fillActiveTimer(char *out, size_t outLen) {
  uint32_t ms = 0;
  if (faultsLeakPhase() == LEAK_PHASE_WAIT) ms = faultsLeakWaitMsRemaining();
  else if (intakeRawWaitActive()) ms = intakeRawWaitRemainingMs();
  if (ms == 0) {
    snprintf(out, outLen, "00:00");
    return;
  }
  formatTimerMmSs(ms, out, outLen);
}

static bool gTdsFaultLatched = false;

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
static uint32_t gLastDigitalMs = 0;
static char gStatusBuf[STATUS_JSON_BUF_SIZE];
// Reused JSON docs — avoid heap churn / fragmentation each broadcast
static JsonDocument gStatusDoc;
static JsonDocument gCmdDoc;
static JsonDocument gApiDoc;

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
  uint8_t leakPhase;
  uint16_t leakWaitSec;
  uint16_t leakCount24h;
  uint16_t leakCountTotal;
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
  int16_t tds1X10;         // 0.1 ppm (0 if invalid)
  int16_t tds2X10;
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
  t.leakPhase = (uint8_t)faultsLeakPhase();
  {
    const uint32_t lms = faultsLeakWaitMsRemaining();
    t.leakWaitSec = (uint16_t)((lms + 999UL) / 1000UL);
  }
  t.leakCount24h = faultsLeakCount24h();
  t.leakCountTotal = faultsLeakCountTotal();
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
  t.tds1X10 = s.tds1Valid ? (int16_t)lroundf(s.tds1Ppm * 10.0f) : (int16_t)-1;
  t.tds2X10 = s.tds2Valid ? (int16_t)lroundf(s.tds2Ppm * 10.0f) : (int16_t)-1;
  t.eventGen = eventLogGeneration();
  return t;
}

static bool snapChanged(const TelemetrySnap &a, const TelemetrySnap &b) {
  return memcmp(&a, &b, sizeof(TelemetrySnap)) != 0;
}

static void sendCommandResult(const char *type, uint8_t channel, bool ok) {
  gCmdDoc.clear();
  gCmdDoc["calibResult"]["type"] = type;
  gCmdDoc["calibResult"]["channel"] = channel;
  gCmdDoc["calibResult"]["ok"] = ok;
  size_t n = serializeJson(gCmdDoc, gStatusBuf, sizeof(gStatusBuf));
  if (n > 0 && n < sizeof(gStatusBuf)) {
    gStatusBuf[n] = '\0';
    webServerBroadcast(gStatusBuf, n);
  }
}

static uint8_t gPendingCalibChannel = 0;
static char gPendingCalibType[8] = "";

static void serviceTdsCalibResult() {
  bool ok = false;
  if (!tdsCalibTakeResult(&ok)) return;
  if (gPendingCalibType[0]) {
    sendCommandResult(gPendingCalibType, gPendingCalibChannel, ok);
    gPendingCalibType[0] = '\0';
  }
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
  } else if (strcmp(c, "reset_leak_wait") == 0) {
    faultsResetLeakWait();
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
  } else if (strcmp(c, "technician_reset") == 0 || strcmp(c, "reset_technician") == 0 ||
             strcmp(c, "system_reset") == 0 || strcmp(c, "reset_system") == 0) {
    faultsTechnicianReset();
    requestBroadcast();
  } else if (strcmp(c, "unlock") == 0 || strcmp(c, "clear_lock") == 0 ||
             strcmp(c, "unlock_hard") == 0) {
    faultsClearHardLocks();
    requestBroadcast();
  } else if (strcmp(c, "calibrate_ec") == 0) {
    // Non-blocking: result arrives later via serviceTdsCalibResult()
    gPendingCalibChannel = (uint8_t)cmd["channel"];
    strncpy(gPendingCalibType, "ec", sizeof(gPendingCalibType) - 1);
    if (!tdsCalibStartConductivity(gPendingCalibChannel, cmd["value"])) {
      sendCommandResult("ec", gPendingCalibChannel, false);
      gPendingCalibType[0] = '\0';
    }
  } else if (strcmp(c, "calibrate_temp") == 0) {
    gPendingCalibChannel = (uint8_t)cmd["channel"];
    strncpy(gPendingCalibType, "temp", sizeof(gPendingCalibType) - 1);
    if (!tdsCalibStartTemperature(gPendingCalibChannel, cmd["value"])) {
      sendCommandResult("temp", gPendingCalibChannel, false);
      gPendingCalibType[0] = '\0';
    }
  } else if (strcmp(c, "calibrate_pressure") == 0) {
    const bool ok = analogBenchCalibratePressure(cmd["value"].as<float>());
    sendCommandResult("pressure", 0, ok);
    requestBroadcast();
  } else if (strcmp(c, "calibrate_vsolar") == 0) {
    const bool ok = analogBenchCalibrateVSolar(cmd["value"].as<float>());
    sendCommandResult("vsolar", 0, ok);
    requestBroadcast();
  } else if (strcmp(c, "set_time") == 0) {
    // Soft clock until hardware RTC: phone sends Unix epoch; kept until power loss.
    bool ok = false;
    const bool autoSync = cmd["auto"].as<bool>();
    if (!cmd["epoch"].isNull()) {
      time_t epoch = (time_t)cmd["epoch"].as<long>();
      if (epoch > 1700000000L) {
        struct timeval tv;
        tv.tv_sec = epoch;
        tv.tv_usec = 0;
        ok = (settimeofday(&tv, nullptr) == 0);
      }
    }
    if (!autoSync) sendCommandResult("time", 0, ok);
    if (ok) requestBroadcast();
  }
}

/** Spec GET /api/status — compact codes only (no Farsi, no full telemetry). */
static size_t buildApiStatusJson(char *buf, size_t cap) {
  gApiDoc.clear();
  char timer[8];
  fillActiveTimer(timer, sizeof(timer));
  const char *code = resolveActiveCode();

  gApiDoc["mode"] = opModeNumber(systemControlOpMode());
  gApiDoc["sub_mode"] = subModeString();
  gApiDoc["active_code"] = code ? code : "";
  gApiDoc["timer"] = timer;
  gApiDoc["night_light"] = systemControlNightLightOn();

  size_t n = serializeJson(gApiDoc, buf, cap);
  if (n == 0 || n >= cap) return 0;
  buf[n] = '\0';
  return n;
}

/** Slim status JSON — event codes only; Farsi on the Web App. */
static void broadcastStatus(const AppSensors &s, uint32_t waitMs, bool includeEvents) {
  gStatusDoc.clear();
  char timer[8];
  fillActiveTimer(timer, sizeof(timer));
  const char *activeCode = resolveActiveCode();

  gStatusDoc["mode"] = opModeNumber(systemControlOpMode());
  gStatusDoc["sub_mode"] = subModeString();
  gStatusDoc["active_code"] = activeCode ? activeCode : "";
  gStatusDoc["timer"] = timer;
  gStatusDoc["night_light"] = systemControlNightLightOn();

  gStatusDoc["scenario"] = scenarioName();
  gStatusDoc["setupNeeded"] = !scenarioIsConfigured();
  gStatusDoc["hostname"] = MDNS_HOSTNAME;
  gStatusDoc["systemEnabled"] = systemControlIsEnabled();
  gStatusDoc["opMode"] = opModeCode(systemControlOpMode());
  gStatusDoc["opModeLabel"] = systemControlOpModeLabel();
  gStatusDoc["standbyReason"] = standbyReasonCode(systemControlStandbyReason());
  gStatusDoc["nightLight"] = systemControlNightLightOn();
  gStatusDoc["routine"] = routineName(systemControlRoutine());
  gStatusDoc["intakePhase"] = intakePhaseName();
  gStatusDoc["purify"] = purifyStateName();
  gStatusDoc["fault"] = faultsName(systemControlFault());
  gStatusDoc["activeCode"] = activeCode ? activeCode : "";
  gStatusDoc["eventGen"] = eventLogGeneration();
  gStatusDoc["locked"] = systemControlIsLocked();
  gStatusDoc["dryRunRetries"] = systemControlDryRunRetries();
  gStatusDoc["intakeRawFails"] = intakeRawFailCount();
  gStatusDoc["intakeWaitActive"] = intakeRawWaitActive();
  gStatusDoc["intakeWaitMs"] = waitMs;
  gStatusDoc["intakeWaitSec"] = (waitMs + 999UL) / 1000UL;
  {
    const uint32_t leakWaitMs = faultsLeakWaitMsRemaining();
    gStatusDoc["leakPhase"] = faultsLeakPhaseName();
    gStatusDoc["leakHardLock"] = faultsLeakHardLocked();
    gStatusDoc["leakWaitActive"] = (faultsLeakPhase() == LEAK_PHASE_WAIT);
    gStatusDoc["leakWaitMs"] = leakWaitMs;
    gStatusDoc["leakWaitSec"] = (leakWaitMs + 999UL) / 1000UL;
    gStatusDoc["leakCount24h"] = faultsLeakCount24h();
    gStatusDoc["leakCountTotal"] = faultsLeakCountTotal();
  }
  gStatusDoc["vSolar"] = s.vSolar;
  gStatusDoc["soc"] = s.socPercent;
  gStatusDoc["irradiancePct"] = plantIrradiancePct();
  gStatusDoc["dayBand"] = plantDayBandActive();
  gStatusDoc["tankPressureBar"] = s.tankPressureBar;
  gStatusDoc["pressureAdc"] = benchPressureAdcVolts();
  gStatusDoc["vSolarAdc"] = benchVSolarAdcVolts();
  gStatusDoc["bench"]["enabled"] = (bool)BENCH_SIMULATION_MODE;
  {
    time_t nowSec = time(nullptr);
    if (nowSec > 1700000000L) gStatusDoc["epoch"] = (long)nowSec;
  }

  JsonObject inputs = gStatusDoc["inputs"].to<JsonObject>();
  inputs["pressureOk"] = s.pressureOk;
  inputs["tankFull"] = s.tankFull;
  inputs["leak"] = s.leak;

  JsonObject relays = gStatusDoc["relays"].to<JsonObject>();
  relays["r1"] = relay1IsOn();
  relays["r2"] = relay2IsOn();
  relays["purify"] = purificationIsOn();
  relays["night"] = nightLightIsOn();

  JsonObject pumps = gStatusDoc["pumps"].to<JsonObject>();
  pumps["treatment"] = purificationIsOn();
  pumps["uv"] = purificationIsOn();
  pumps["raw"] = relay1IsOn();

  if (s.tds1Valid) {
    JsonObject t = gStatusDoc["tds1"].to<JsonObject>();
    t["ec"] = s.ec1;
    t["temp"] = s.temp1C;
    t["tds"] = s.tds1Ppm;
  }
  if (s.tds2Valid) {
    JsonObject t = gStatusDoc["tds2"].to<JsonObject>();
    t["ec"] = s.ec2;
    t["temp"] = s.temp2C;
    t["tds"] = s.tds2Ppm;
  }

  if (includeEvents) {
    // All codes live in the NVS ring (survive reboot)
    JsonArray logs = gStatusDoc["events"].to<JsonArray>();
    const uint8_t nEv = eventLogCount();
    for (uint8_t i = 0; i < nEv; i++) {
      EventLogEntry e = eventLogGet(i);
      if (!e.code[0]) continue;
      JsonObject o = logs.add<JsonObject>();
      o["code"] = e.code;
      o["count"] = e.counter;
      o["ms"] = e.millisStamp;
      if (e.epochStamp) o["epoch"] = e.epochStamp;
    }
  }

  size_t n = serializeJson(gStatusDoc, gStatusBuf, sizeof(gStatusBuf));
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

  // Always attach events on first snap / gen change / force / heartbeat.
  // (Omitting events left the Alerts page empty until the next gen bump;
  // oversized JSON previously failed the whole broadcast — buffer enlarged.)
  const bool eventsDirty = !gHaveSnap || (snap.eventGen != gLastSnap.eventGen);
  broadcastStatus(s, waitMs, eventsDirty || gForceBroadcast || heartbeat);
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
  modbusTracerInit();
  webServerInit();
  webServerOnCommand(handleWsCommand);
  webServerOnStatus(buildApiStatusJson);
  tdsInit();
  gLastBroadcastMs = 0;
  gLastDigitalMs = 0;
}

void loop() {
  const uint32_t now = millis();

  // Digital inputs @ 100 ms (debounce already inside module)
  if ((now - gLastDigitalMs) >= DIGITAL_POLL_MS) {
    gLastDigitalMs = now;
    digitalInputsUpdate();
  }

  analogBenchUpdate();
  modbusTracerPoll();          // 1000 ms gated internally (stub in bench)
  (void)tdsPoll();             // non-blocking UART state machine @ TDS_POLL_MS
  serviceTdsCalibResult();

  // Refresh cached TDS samples (last-good kept across timeouts)
  {
    float ec = 0, temp = 0, tds = 0;
    if (tdsGetLast(1, ec, temp, tds)) {
      gLastTds.tds1Valid = true;
      gLastTds.ec1 = ec;
      gLastTds.temp1C = temp;
      gLastTds.tds1Ppm = tds;
      gTdsFaultLatched = false;
    }
    if (tdsGetLast(2, ec, temp, tds)) {
      gLastTds.tds2Valid = true;
      gLastTds.ec2 = ec;
      gLastTds.temp2C = temp;
      gLastTds.tds2Ppm = tds;
      gTdsFaultLatched = false;
    }
    if (!gTdsFaultLatched && tdsFailStreak() >= 10 &&
        !gLastTds.tds1Valid && !gLastTds.tds2Valid) {
      eventLogEmit(CODE_E105);
      gTdsFaultLatched = true;
    }
  }

  DigitalInputState in = digitalInputsGet();
  AppSensors s = {};
  s.pressureOk = in.pressureOk;
  s.tankFull = in.tankFull;
  s.leak = in.leakDetected;
  s.vSolar = plantVSolar();
  s.socPercent = plantSocPercent();
  s.tankPressureBar = tankPressureBar();
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
  webServerLoop();
  maybeBroadcast(s);

  yield();  // WiFi/TCP — never delay() in loop
}
