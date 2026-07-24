#ifndef PURIFY_H
#define PURIFY_H

#include <Arduino.h>

void purifyInit();
void purifyUpdate(bool systemEnabled, bool faultsLocked, bool intakeBlocked);

bool purifyIsRunning();
const char *purifyStateName();  // "idle" | "purifying"

#endif // PURIFY_H
