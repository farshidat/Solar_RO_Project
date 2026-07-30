#include "scenario.h"
#include <Preferences.h>
#include <esp_system.h>

static Preferences prefs;
static SystemScenario current = SCENARIO_UNSET;

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
    return;
  }

  // First boot: leave UNSET — SoftAP + captive portal until user selects A/B in Web App
  current = SCENARIO_UNSET;
}

SystemScenario scenarioGet() { return current; }

bool scenarioIsConfigured() {
  return current == SCENARIO_A || current == SCENARIO_B;
}

const char *scenarioName() {
  if (current == SCENARIO_A) return "Scenario_A";
  if (current == SCENARIO_B) return "Scenario_B";
  return "Unset";
}

bool scenarioIsA() { return current == SCENARIO_A; }
bool scenarioIsB() { return current == SCENARIO_B; }

void scenarioSet(SystemScenario s) {
  if (s != SCENARIO_A && s != SCENARIO_B) return;
  const bool firstConfigure = (current == SCENARIO_UNSET);
  saveScenario(s);
  if (firstConfigure) {
    delay(400);  // let WS flush scenario ack
    ESP.restart();
  }
}
