#ifndef PLANT_POWER_H
#define PLANT_POWER_H

#include "config.h"

inline float plantVSolar() {
#if PHASE1_IGNORE_VSOLAR
  return 24.0f;  // TODO Phase 2: ADS1115 isolated measurement
#else
  return 0.0f;
#endif
}

inline bool plantSolarAbove(float thresholdV) {
  return plantVSolar() > thresholdV;
}

#endif
