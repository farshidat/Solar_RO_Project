#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "tds_sensor.h"
#include "relay_control.h"
#include "web_server.h"
#include "digital_inputs.h"
#include "scenario.h"
#include "system_control.h"

static const char *routineName(ActiveRoutine r) {
  switch (r) {
    case ROUTINE_INTAKE: return "intake";
    case ROUTINE_PURIFYING: return "purifying";
    case ROUTINE_DRY_RUN_WAIT: return "dry_run_wait";
    case ROUTINE_LOCKED: return "locked";
    default: return "idle";
  }
}

static const char *faultName(SystemFault f) {
  switch (f) {
    case FAULT_LEAK: return "leak";
    case FAULT_DRY_RUN: return "dry_run";
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
    bool on = cmd["on"].as<bool>();
    systemControlSetEnabled(on);
  } else if (strcmp(c, "scenario") == 0) {
    const char *mode = cmd["mode"];
    if (mode && (mode[0] == 'A' || mode[0] == 'a')) scenarioSet(SCENARIO_A);
    else if (mode && (mode[0] == 'B' || mode[0] == 'b')) scenarioSet(SCENARIO_B);
  } else if (strcmp(c, "raw_pump") == 0) {
    bool on = cmd["on"].as<bool>();
    systemControlRequestRelay1(on);
  } else if (strcmp(c, "calibrate_ec") == 0) {
    uint8_t channel = cmd["channel"];
    float value = cmd["value"];
    sendCommandResult("ec", channel, tdsCalibrateConductivity(channel, value));
  } else if (strcmp(c, "calibrate_temp") == 0) {
    uint8_t channel = cmd["channel"];
    float value = cmd["value"];
    sendCommandResult("temp", channel, tdsCalibrateTemperature(channel, value));
  }
}

static void broadcastStatus() {
  DigitalInputState in = digitalInputsGet();

  float ec1 = 0, temp1 = 0, tds1 = 0;
  float ec2 = 0, temp2 = 0, tds2 = 0;
  bool ok1 = tdsRead(1, ec1, temp1, tds1);
  bool ok2 = tdsRead(2, ec2, temp2, tds2);

  JsonDocument doc;
  doc["scenario"] = scenarioName();
  doc["systemEnabled"] = systemControlIsEnabled();
  doc["routine"] = routineName(systemControlRoutine());
  doc["fault"] = faultName(systemControlFault());
  doc["locked"] = systemControlIsLocked();
  doc["dryRunRetries"] = systemControlDryRunRetries();
  doc["inputs"]["pressureOk"] = in.pressureOk;
  doc["inputs"]["tankFull"] = in.tankFull;
  doc["inputs"]["leak"] = in.leakDetected;
  doc["relays"]["r1"] = relay1IsOn();
  doc["relays"]["r2"] = relay2IsOn();
  doc["relays"]["purify"] = purificationIsOn();
  doc["relays"]["night"] = nightLightIsOn();
  doc["pumps"]["treatment"] = purificationIsOn();
  doc["pumps"]["uv"] = purificationIsOn();
  doc["pumps"]["raw"] = relay1IsOn();
  if (ok1) {
    doc["tds1"]["ec"] = ec1;
    doc["tds1"]["temp"] = temp1;
    doc["tds1"]["tds"] = tds1;
  }
  if (ok2) {
    doc["tds2"]["ec"] = ec2;
    doc["tds2"]["temp"] = temp2;
    doc["tds2"]["tds"] = tds2;
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
  scenarioInit();
  systemControlInit();
  webServerInit();
  webServerOnCommand(handleWsCommand);
  tdsInit();
}

void loop() {
  digitalInputsUpdate();
  systemControlUpdate();
  broadcastStatus();
  delay(300);
}
