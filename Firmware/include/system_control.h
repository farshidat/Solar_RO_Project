#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <Arduino.h>

enum ActiveRoutine : uint8_t {
  ROUTINE_IDLE = 0,
  ROUTINE_INTAKE,       // Scenario B: raw pump filling / building pressure
  ROUTINE_PURIFYING,
  ROUTINE_DRY_RUN_WAIT,
  ROUTINE_LOCKED,
};

enum SystemFault : uint8_t {
  FAULT_NONE = 0,
  FAULT_LEAK,
  FAULT_DRY_RUN,
};

void systemControlInit();
void systemControlUpdate();

ActiveRoutine systemControlRoutine();
SystemFault systemControlFault();
bool systemControlIsLocked();
bool systemControlIsEnabled();
uint8_t systemControlDryRunRetries();

void systemControlSetEnabled(bool on);
void systemControlRequestPurification(bool on);
void systemControlRequestRelay1(bool on);

#endif // SYSTEM_CONTROL_H
