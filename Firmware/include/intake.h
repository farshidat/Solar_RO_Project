#ifndef INTAKE_H
#define INTAKE_H

#include <Arduino.h>

enum IntakePhase : uint8_t {
  INTAKE_IDLE = 0,
  INTAKE_NORMAL,
  INTAKE_WAIT_30M,
  INTAKE_FLUSH,
  INTAKE_STANDBY_SOLAR,
  INTAKE_RAW_DRY_WAIT,
};

void intakeInit();
void intakeUpdate(bool systemEnabled, bool solarOk);

IntakePhase intakePhase();
bool intakeBlocksPurify();
const char *intakePhaseName();

bool intakeRawWaitActive();
uint32_t intakeRawWaitRemainingMs();
uint8_t intakeRawFailCount();
void intakeResetRawWait();  // UI confirm reset of 30-min wait

#endif
