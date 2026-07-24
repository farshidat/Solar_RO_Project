#ifndef PLANT_POWER_H
#define PLANT_POWER_H

#include <Arduino.h>
#include "config.h"

// Phase-1 stubs. Replace with ADS1115 V_solar in Phase 2.
inline float plantVSolar() {
#if PHASE1_IGNORE_VSOLAR
  return 24.0f;
#else
  return 0.0f;
#endif
}

inline bool plantSolarAbove(float thresholdV) {
  return plantVSolar() > thresholdV;
}

#endif // PLANT_POWER_H
