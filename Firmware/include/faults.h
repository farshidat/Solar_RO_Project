#ifndef FAULTS_H
#define FAULTS_H

#include <Arduino.h>

enum FaultId : uint8_t {
  FAULT_NONE = 0,
  FAULT_LEAK,
  FAULT_DRY_RUN,       // Scenario A purify dry-run
  FAULT_INTAKE_DRY,    // Scenario B raw pump dry-run hard lock
  FAULT_UV,
  FAULT_PREFILTER,
  FAULT_MEMBRANE,
};

void faultsInit();
void faultsUpdate(bool systemEnabled);

bool faultsIsLocked();
bool faultsInDryRunWait();  // A purify retry wait
FaultId faultsActive();
uint8_t faultsDryRunRetries();
bool faultsMembraneTestActive();
uint8_t faultsMembraneTestStep();
float faultsUvHours();
float faultsPrefilterLiters();

void faultsForceLock(FaultId id, const char *logMsg);

void faultsResetUvCounter();
void faultsResetPrefilterVolume();
void faultsResetMembraneTest();

const char *faultsName(FaultId id);

#endif
