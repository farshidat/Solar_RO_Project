#ifndef ANALOG_BENCH_H
#define ANALOG_BENCH_H

#include <Arduino.h>

// Temporary pot simulation / future Modbus+transducer facade.
// Core state machine only reads plantVSolar() / tankPressureBar().

void analogBenchInit();
void analogBenchUpdate();  // 1 Hz sample into 20-point moving average

float plantVSolar();
float plantSocPercent();
bool plantSolarAbove(float thresholdV);

float tankPressureBar();
float benchVSolarAdcVolts();   // filtered ADC volts at GPIO34 (0–3.3)
float benchPressureAdcVolts(); // filtered ADC volts at GPIO35 (0–3.3)

#endif
