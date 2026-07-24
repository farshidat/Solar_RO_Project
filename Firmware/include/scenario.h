#ifndef SCENARIO_H
#define SCENARIO_H

#include <Arduino.h>

enum SystemScenario : uint8_t {
  SCENARIO_UNSET = 0,
  SCENARIO_A = 1,  // Mains / tap water
  SCENARIO_B = 2,  // Raw pump + 40L pressure tank
};

void scenarioInit();                 // load from NVS (default B on first boot)
SystemScenario scenarioGet();
const char *scenarioName();
bool scenarioIsA();
bool scenarioIsB();
void scenarioSet(SystemScenario s);  // save to NVS

#endif // SCENARIO_H
