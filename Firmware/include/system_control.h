#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <Arduino.h>
#include "faults.h"

enum OpMode : uint8_t {
  STATE_NIGHT = 0,
  STATE_STANDBY,
  STATE_ACTIVE,
};

enum ActiveRoutine : uint8_t {
  ROUTINE_IDLE = 0,
  ROUTINE_INTAKE,
  ROUTINE_PURIFYING,
  ROUTINE_DRY_RUN_WAIT,
  ROUTINE_LOCKED,
};

enum StandbyReason : uint8_t {
  STANDBY_NONE = 0,
  STANDBY_TANK_FULL,
  STANDBY_NO_RAW_WATER,
  STANDBY_FAULT,
  STANDBY_OTHER,
};

void systemControlInit();
void systemControlUpdate();

OpMode systemControlOpMode();
const char *systemControlOpModeLabel();  // Persian UI string
StandbyReason systemControlStandbyReason();
bool systemControlNightLightOn();

ActiveRoutine systemControlRoutine();
FaultId systemControlFault();
bool systemControlIsLocked();
bool systemControlIsEnabled();
uint8_t systemControlDryRunRetries();

void systemControlSetEnabled(bool on);
void systemControlRequestPurification(bool on);
void systemControlRequestRelay1(bool on);

#endif
