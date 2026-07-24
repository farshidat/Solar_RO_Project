#include "scenario.h"
#include <Preferences.h>

static Preferences prefs;
static SystemScenario current = SCENARIO_UNSET;

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

static void saveScenario(SystemScenario s) {
  prefs.begin("solar_ro", false);
  prefs.putUChar("sys_mode", (uint8_t)s);
  prefs.end();
  current = s;
}

void scenarioInit() {
  prefs.begin("solar_ro", true);
  uint8_t stored = prefs.getUChar("sys_mode", SCENARIO_UNSET);
  prefs.end();

  if (stored == SCENARIO_A || stored == SCENARIO_B) {
    current = (SystemScenario)stored;
    Serial.printf("System mode from NVS: %s\n", scenarioName());
    return;
  }

  Serial.println("=== First boot: select system mode ===");
  Serial.println("Enter A = Mains/Tap water");
  Serial.println("Enter B = Raw pump + 40L pressure tank");

  while (true) {
    String line;
    if (!waitForLine(line, 60000)) {
      Serial.println("No input yet. Waiting for A or B...");
      continue;
    }
    line.toUpperCase();
    if (line == "A") {
      saveScenario(SCENARIO_A);
      Serial.println("Saved Scenario A. Restarting...");
      delay(500);
      ESP.restart();
    }
    if (line == "B") {
      saveScenario(SCENARIO_B);
      Serial.println("Saved Scenario B. Restarting...");
      delay(500);
      ESP.restart();
    }
    Serial.println("Invalid. Enter A or B.");
  }
}

SystemScenario scenarioGet() { return current; }

const char *scenarioName() {
  if (current == SCENARIO_A) return "Scenario_A";
  if (current == SCENARIO_B) return "Scenario_B";
  return "Unset";
}

bool scenarioIsA() { return current == SCENARIO_A; }
bool scenarioIsB() { return current == SCENARIO_B; }
