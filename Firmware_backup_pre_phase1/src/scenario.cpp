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

  // Do NOT block forever here — that prevented WiFi/AP from starting.
  Serial.println("=== First boot: select system mode (5s) ===");
  Serial.println("Enter A = Mains/Tap water");
  Serial.println("Enter B = Raw pump + 40L pressure tank");
  Serial.println("If no input, default = Scenario B (WiFi will start).");

  String line;
  if (waitForLine(line, 5000)) {
    line.toUpperCase();
    if (line == "A") {
      saveScenario(SCENARIO_A);
      Serial.println("Saved Scenario A.");
      return;
    }
    if (line == "B") {
      saveScenario(SCENARIO_B);
      Serial.println("Saved Scenario B.");
      return;
    }
    Serial.println("Invalid input. Using default Scenario B.");
  } else {
    Serial.println("No Serial input. Using default Scenario B.");
  }

  saveScenario(SCENARIO_B);
}

SystemScenario scenarioGet() { return current; }

const char *scenarioName() {
  if (current == SCENARIO_A) return "Scenario_A";
  if (current == SCENARIO_B) return "Scenario_B";
  return "Unset";
}

bool scenarioIsA() { return current == SCENARIO_A; }
bool scenarioIsB() { return current == SCENARIO_B; }
