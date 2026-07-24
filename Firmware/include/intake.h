#ifndef INTAKE_H
#define INTAKE_H

#include <Arduino.h>

enum IntakePhase : uint8_t {
  INTAKE_PHASE_IDLE = 0,       // / normal standby
  INTAKE_PHASE_NORMAL,         // flowing OK
  INTAKE_PHASE_WAIT_30M,
  INTAKE_PHASE_FLUSH,
  INTAKE_PHASE_STANDBY_SOLAR,  // B only
};

void intakeInit();
void intakeUpdate(bool systemEnabled, bool faultsLocked, float tds1Ppm, bool tds1Valid);

IntakePhase intakePhase();
bool intakeBlocksPurify();   // true during wait/flush/error handling
const char *intakePhaseName();

#endif // INTAKE_H
