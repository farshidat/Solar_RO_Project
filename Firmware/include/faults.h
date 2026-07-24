#ifndef FAULTS_H
#define FAULTS_H

#include <Arduino.h>

enum FaultId : uint8_t {
  FAULT_NONE = 0,
  FAULT_LEAK,
  FAULT_DRY_RUN,
  FAULT_UV,
  FAULT_PREFILTER,
  FAULT_MEMBRANE,
};

void faultsInit();
void faultsUpdate(bool systemEnabled, float tds2Ppm, bool tds2Valid);

bool faultsIsLocked();
bool faultsInDryRunWait();
FaultId faultsActive();
uint8_t faultsDryRunRetries();
bool faultsMembraneTestActive();
uint8_t faultsMembraneTestStep();  // 0..5

float faultsUvHours();
float faultsPrefilterLiters();

// Soft resets after physical replacement (not for leak/dry-run lock)
void faultsResetUvCounter();
void faultsResetPrefilterVolume();
void faultsResetMembraneTest();

// Called by purify path awareness (optional); dry-run watches Relay3+pressure directly
void faultsNotifyRelay3Running(bool on);

#endif // FAULTS_H
