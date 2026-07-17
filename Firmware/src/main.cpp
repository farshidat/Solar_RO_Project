#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "tds_sensor.h"
#include "relay_control.h"
#include "web_server.h"

// main.cpp only orchestrates: it calls each module's functions and decides
// where to show the results (Serial Monitor for now; display/web/app later).

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
      Serial.println("Note: if your standard solution label is in TDS ppm, multiply it by ~2.");
      String valStr;
      if (!waitForLine(valStr, 30000) || valStr.length() == 0) { Serial.println("No value given. No change made."); return; }
      float refEC = valStr.toFloat();
      Serial.println("Calibrating conductivity...");
      bool ok = tdsCalibrateConductivity((uint8_t)channel, refEC);
      Serial.println(ok ? "Calibration successful." : "Calibration failed.");
    } else if (typeStr == "N") {
      Serial.println("Enter reference temperature in C (e.g. 25.0).");
      String valStr;
      if (!waitForLine(valStr, 30000) || valStr.length() == 0) { Serial.println("No value given. No change made."); return; }
      float refTemp = valStr.toFloat();
      Serial.println("Calibrating temperature (NTC)...");
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

// فرمان‌های دریافتی از وب‌اپ (لایه پیام): از JSON به فراخوانی توابع ماژول‌ها تبدیل می‌شود
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

  if (strcmp(c, "power") == 0) {
    bool on = cmd["on"];
    if (on) { pumpOn(); uvOn(); } else { pumpOff(); uvOff(); }
    Serial.printf("System power command: %s\n", on ? "ON" : "OFF");
  } else if (strcmp(c, "raw_pump") == 0) {
    bool on = cmd["on"];
    if (on) rawPumpOn(); else rawPumpOff();
    Serial.printf("Raw water pump command: %s\n", on ? "ON" : "OFF");
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
  relayInit();
  tdsInit();
  webServerInit();
  webServerOnCommand(handleWsCommand);

  Serial.println("=== Phase 2: Dual-Channel TDS Module ===");
  Serial.println("Type anything and press Enter within 10 seconds to run calibration...");

  String trigger;
  if (waitForLine(trigger, 10000)) {
    runCalibrationMenu();
  } else {
    Serial.println("No input. Skipping calibration. No change made.");
  }

  Serial.println("Entering normal display mode.");
}

void loop() {
  float ec1, temp1, tds1;
  float ec2, temp2, tds2;

  bool ok1 = tdsRead(1, ec1, temp1, tds1);
  if (ok1) {
    Serial.printf("Channel 1 (Inlet)  | Temp: %.1f C | EC: %.1f us/cm | TDS: %.1f ppm\n", temp1, ec1, tds1);
  } else {
    Serial.println("Channel 1: no response (timeout)");
  }

  bool ok2 = tdsRead(2, ec2, temp2, tds2);
  if (ok2) {
    Serial.printf("Channel 2 (Outlet)  | Temp: %.1f C | EC: %.1f us/cm | TDS: %.1f ppm\n", temp2, ec2, tds2);
  } else {
    Serial.println("Channel 2: no response (timeout)");
  }

  JsonDocument doc;
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
  doc["pumps"]["treatment"] = pumpIsOn();
  doc["pumps"]["uv"] = uvIsOn();
  doc["pumps"]["raw"] = rawPumpIsOn();
  String out;
  serializeJson(doc, out);
  webServerBroadcast(out);

  Serial.println("--------------------------------------------------");
  delay(2000);
}
