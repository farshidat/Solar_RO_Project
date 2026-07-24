#ifndef DIGITAL_INPUTS_H
#define DIGITAL_INPUTS_H

#include <Arduino.h>

struct DigitalInputState {
  bool pressureOk;   // true = > 2 bar
  bool tankFull;     // true = product tank full
  bool leakDetected; // true = water leak (active low pin)
};

void digitalInputsInit();
void digitalInputsUpdate();
DigitalInputState digitalInputsGet();

#endif // DIGITAL_INPUTS_H
