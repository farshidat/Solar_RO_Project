#ifndef TDS_SENSOR_H
#define TDS_SENSOR_H

#include <Arduino.h>

void tdsInit();

// Reads a channel (1 or 2). ec = raw conductivity (us/cm), tdsPpm = ec/2.
bool tdsRead(uint8_t channel, float &ec, float &temperature, float &tdsPpm);

// Single-point calibration. referenceEC_usPerCm is the standard solution's
// conductivity in us/cm (NOT TDS ppm - multiply a ppm-labeled solution by ~2).
bool tdsCalibrateConductivity(uint8_t channel, float referenceEC_usPerCm);

// Single-point NTC (temperature) calibration against a known reference temperature.
bool tdsCalibrateTemperature(uint8_t channel, float referenceTempC);

#endif // TDS_SENSOR_H
