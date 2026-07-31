#ifndef FAULTS_H
#define FAULTS_H

#include <Arduino.h>

enum FaultId : uint8_t {
  FAULT_NONE = 0,
  FAULT_LEAK,
  FAULT_DRY_RUN,       // Legacy A purify dry-run (not active in SM)
  FAULT_INTAKE_DRY,    // Legacy B hard lock (not active)
  FAULT_UV,
  FAULT_PREFILTER,
  FAULT_MEMBRANE,
};

/** Leak protection phases (E101 / O306). */
enum LeakPhase : uint8_t {
  LEAK_PHASE_CLEAR = 0,
  LEAK_PHASE_ACTIVE,      // E101_ACTIVE — GPIO14 LOW
  LEAK_PHASE_WAIT,        // O306_LEAK_WAIT — 20 min dry-out
  LEAK_PHASE_HARD_LOCK,   // E101_HARD_LOCK — technician reset only
};

void faultsInit();
void faultsUpdate(bool systemEnabled);

bool faultsIsLocked();
bool faultsInDryRunWait();  // A purify retry wait
FaultId faultsActive();
LeakPhase faultsLeakPhase();
uint32_t faultsLeakWaitMsRemaining();
uint16_t faultsLeakCount24h();
uint16_t faultsLeakCountTotal();
uint8_t faultsDryRunRetries();
bool faultsMembraneTestActive();
uint8_t faultsMembraneTestStep();
float faultsUvHours();
float faultsPrefilterLiters();

void faultsForceLock(FaultId id, const char *logMsg);

void faultsResetUvCounter();
void faultsResetPrefilterVolume();
void faultsResetMembraneTest();

/** Clears all hard lockouts + leak counters (Settings → ریست کارشناس). */
void faultsTechnicianReset();

/** Status code string: E101_ACTIVE / O306_LEAK_WAIT / E101_HARD_LOCK / uv / … */
const char *faultsName(FaultId id);

#endif
