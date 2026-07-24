#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "tds_sensor.h"
#include "relay_control.h"
#include "web_server.h"
#include "digital_inputs.h"
#include "scenario.h"
#include "system_control.h"

static bool waitForLine(String &line, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (Serial.available()) {
      line = Serial.readStringUntil('\n');
      line.trim();
      return true;
    }
  }
  return false;
}

static const char *routineName(ActiveRoutine r) {
  switch (r) {
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

static void runCalibrationMenu() {
  while (true) {
    Serial.println("Calibration - Channel? (1 or 2)");
    String chStr;
    if (!waitForLine(chStr, 30000)) { Serial.println("Timeout. Exiting calibration."); return; }
    int channel = chStr.toInt();
    if (channel != 1 && channel != 2) { Serial.println("Invalid channel. Exiting calibration."); return; }

    Serial.println("Type? (T = TDS/conductivity, N = temperature/NTC)");
    String typeStr;
    if (!waitForLine(typeStr, 30000)) { Serial.println("Timeout. Exiting calibration."); return; }
    typeStr.toUpperCase();

    if (typeStr == "T") {
      Serial.println("Enter reference conductivity in us/cm (e.g. 1000.0).");
      String valStr;
      if (!waitForLine(valStr, 30000) || valStr.length() == 0) { Serial.println("No value given. No change made."); return; }
      float refEC = valStr.toFloat();
      bool ok = tdsCalibrateConductivity((uint8_t)channel, refEC);
      Serial.println(ok ? "Calibration successful." : "Calibration failed.");
    } else if (typeStr == "N") {
      Serial.println("Enter reference temperature in C (e.g. 25.0).");
      String valStr;
      if (!waitForLine(valStr, 30000) || valStr.length() == 0) { Serial.println("No value given. No change made."); return; }
      float refTemp = valStr.toFloat();
      bool ok = tdsCalibrateTemperature((uint8_t)channel, refTemp);
      Serial.println(ok ? "Calibration successful." : "Calibration failed.");
    } else {
      Serial.println("Invalid type. Exiting calibration.");
      return;
    }

    Serial.println("Calibrate another? (y = yes, anything else = done)");
    String again;
    if (!waitForLine(again, 30000)) return;
    again.toUpperCase();
    if (again != "Y") return;
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
    bool on = cmd["on"];
    systemControlSetEnabled(on);
    Serial.printf("Master system command: %s\n", on ? "ON" : "OFF");
  } else if (strcmp(c, "raw_pump") == 0) {
    // Kept for debug; UI no longer exposes a separate raw-pump key
    bool on = cmd["on"];
    systemControlRequestRelay1(on);
    Serial.printf("Relay1 command: %s\n", on ? "ON" : "OFF");
  } else if (strcmp(c, "calibrate_ec") == 0) {
    uint8_t channel = cmd["channel"];
    float value = cmd["value"];
    bool ok = tdsCalibrateConductivity(channel, value);
    sendCommandResult("ec", channel, ok);
  } else if (strcmp(c, "calibrate_temp") == 0) {
    uint8_t channel = cmd["channel"];
    float value = cmd["value"];
    bool ok = tdsCalibrateTemperature(channel, value);
    sendCommandResult("temp", channel, ok);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(500);

  Serial.println("=== Solar RO Firmware — Phase 1 (digital inputs + routines) ===");
  Serial.println("V_solar purify gating: TEMPORARILY DISABLED (enable in Phase 2)");

  relayInit();
  digitalInputsInit();
  scenarioInit();  // blocks + restarts if mode not in NVS
  systemControlInit();
  tdsInit();
  webServerInit();
  webServerOnCommand(handleWsCommand);

  Serial.println("Type anything within 10s for TDS calibration, or wait...");
  String trigger;
  if (waitForLine(trigger, 10000)) {
    runCalibrationMenu();
  } else {
    Serial.println("Skipping calibration.");
  }

  Serial.printf("Running. Mode=%s\n", scenarioName());
}

void loop() {
  digitalInputsUpdate();
  systemControlUpdate();

  DigitalInputState in = digitalInputsGet();

  float ec1 = 0, temp1 = 0, tds1 = 0;
  float ec2 = 0, temp2 = 0, tds2 = 0;
  bool ok1 = tdsRead(1, ec1, temp1, tds1);
  bool ok2 = tdsRead(2, ec2, temp2, tds2);

  Serial.printf(
    "Mode=%s Sys=%s | P=%s Float=%s Leak=%s | R1=%d R2=%d Purify=%d Night=%d | Routine=%s Fault=%s Lock=%d DryRetry=%u\n",
    scenarioName(),
    systemControlIsEnabled() ? "ON" : "OFF",
    in.pressureOk ? "OK" : "LOW",
    in.tankFull ? "FULL" : "LOW",
    in.leakDetected ? "YES" : "no",
    relay1IsOn() ? 1 : 0,
    relay2IsOn() ? 1 : 0,
    purificationIsOn() ? 1 : 0,
    nightLightIsOn() ? 1 : 0,
    routineName(systemControlRoutine()),
    faultName(systemControlFault()),
    systemControlIsLocked() ? 1 : 0,
    (unsigned)systemControlDryRunRetries()
  );

  if (ok1) {
    Serial.printf("  TDS1 Temp=%.1f EC=%.1f TDS=%.1f\n", temp1, ec1, tds1);
  }
  if (ok2) {
    Serial.printf("  TDS2 Temp=%.1f EC=%.1f TDS=%.1f\n", temp2, ec2, tds2);
  }

  JsonDocument doc;
  doc["scenario"] = scenarioName();
  doc["systemEnabled"] = systemControlIsEnabled();
  doc["routine"] = routineName(systemControlRoutine());
  doc["fault"] = faultName(systemControlFault());
  doc["locked"] = systemControlIsLocked();
  doc["inputs"]["pressureOk"] = in.pressureOk;
  doc["inputs"]["tankFull"] = in.tankFull;
  doc["inputs"]["leak"] = in.leakDetected;
  doc["relays"]["r1"] = relay1IsOn();
  doc["relays"]["r2"] = relay2IsOn();
  doc["relays"]["purify"] = purificationIsOn();
  doc["relays"]["night"] = nightLightIsOn();
  // Webapp master toggle syncs from systemEnabled (treatment mirrors enable for older UI)
  doc["pumps"]["treatment"] = systemControlIsEnabled();
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

  Serial.println("--------------------------------------------------");
  delay(500);
}
