#include "scenario.h"
#include <Preferences.h>

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

  // First boot: default B so WiFi/AP is never blocked on Serial menus.
  // Mode can be changed later from the Web UI when that command is added.
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

void scenarioSet(SystemScenario s) {
  if (s != SCENARIO_A && s != SCENARIO_B) return;
  saveScenario(s);
}
