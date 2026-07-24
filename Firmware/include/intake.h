#ifndef INTAKE_H
#define INTAKE_H

#include <Arduino.h>

enum IntakePhase : uint8_t {
  INTAKE_IDLE = 0,
  INTAKE_NORMAL,
  INTAKE_WAIT_30M,
  INTAKE_FLUSH,
  INTAKE_STANDBY_SOLAR,
};

void intakeInit();
void intakeUpdate(bool systemEnabled);

IntakePhase intakePhase();
bool intakeBlocksPurify();
const char *intakePhaseName();

#endif
