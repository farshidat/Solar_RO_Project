#ifndef ANALOG_BENCH_H
#define ANALOG_BENCH_H

#include <Arduino.h>

void analogBenchInit();
void analogBenchUpdate();  // ADC sample + day-band hysteresis

float plantVSolar();
float plantIrradiancePct();  // 0–100 from bench mapping
float plantSocPercent();
bool plantDayBandActive();   // hysteresis: enter >35%, exit <30%
bool plantSolarAbove(float thresholdV);  // legacy helper

float tankPressureBar();
float benchVSolarAdcVolts();
float benchPressureAdcVolts();

/** One-point calib: map current ADC volts to the given reference (persisted in NVS). */
bool analogBenchCalibratePressure(float referenceBar);
bool analogBenchCalibrateVSolar(float referenceVolts);

#endif
