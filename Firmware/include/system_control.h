#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <Arduino.h>
#include "faults.h"

enum ActiveRoutine : uint8_t {
  ROUTINE_IDLE = 0,
  ROUTINE_INTAKE,
  ROUTINE_PURIFYING,
  ROUTINE_DRY_RUN_WAIT,
  ROUTINE_LOCKED,
};

void systemControlInit();
void systemControlUpdate(float tds1Ppm, bool tds1Valid, float tds2Ppm, bool tds2Valid);

ActiveRoutine systemControlRoutine();
FaultId systemControlFault();
bool systemControlIsLocked();
bool systemControlIsEnabled();
uint8_t systemControlDryRunRetries();

void systemControlSetEnabled(bool on);
void systemControlRequestPurification(bool on);
void systemControlRequestRelay1(bool on);

#endif // SYSTEM_CONTROL_H
