#ifndef SCENARIO_H
#define SCENARIO_H

#include <Arduino.h>

enum SystemScenario : uint8_t {
  SCENARIO_UNSET = 0,
  SCENARIO_A = 1,  // Mains / tap water
  SCENARIO_B = 2,  // Raw pump + 40L pressure tank
};

void scenarioInit();                 // load from NVS; prompt on Serial if unset
SystemScenario scenarioGet();
const char *scenarioName();
bool scenarioIsA();
bool scenarioIsB();

#endif // SCENARIO_H
