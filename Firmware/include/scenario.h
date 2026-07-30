#ifndef SCENARIO_H
#define SCENARIO_H

#include <Arduino.h>

enum SystemScenario : uint8_t {
  SCENARIO_UNSET = 0,
  SCENARIO_A = 1,  // Mains / tap water
  SCENARIO_B = 2,  // Raw pump + 40L pressure tank
};

void scenarioInit();                 // load from NVS; leave UNSET on first boot
SystemScenario scenarioGet();
const char *scenarioName();
bool scenarioIsConfigured();         // false until user picks A/B (first-run)
bool scenarioIsA();
bool scenarioIsB();
void scenarioSet(SystemScenario s);  // save to NVS; restart on first configure

#endif // SCENARIO_H
