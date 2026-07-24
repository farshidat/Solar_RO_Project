#ifndef PURIFY_H
#define PURIFY_H

#include <Arduino.h>

void purifyInit();
void purifyUpdate(bool systemEnabled);

bool purifyIsRunning();
const char *purifyStateName();

#endif
