#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <Arduino.h>

enum ActiveRoutine : uint8_t {
  ROUTINE_IDLE = 0,
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
void systemControlUpdate();  // call every loop after digitalInputsUpdate()

ActiveRoutine systemControlRoutine();
SystemFault systemControlFault();
bool systemControlIsLocked();
bool systemControlIsEnabled();
uint8_t systemControlDryRunRetries();

// Master system ON/OFF from Web settings (one key for whole plant)
void systemControlSetEnabled(bool on);

// Legacy / advanced overrides (ignored when locked or system disabled)
void systemControlRequestPurification(bool on);
void systemControlRequestRelay1(bool on);  // A: close inlet / B: raw pump

#endif // SYSTEM_CONTROL_H
