#ifndef TDS_SENSOR_H
#define TDS_SENSOR_H

#include <Arduino.h>

void tdsInit();

/** Non-blocking poll — call every loop(); returns true when a channel just completed. */
bool tdsPoll();

/** Consecutive timed-out / bad read frames (reset on success). */
uint8_t tdsFailStreak();

/** Last good sample for a channel (1 or 2). Returns false if never successfully read. */
bool tdsGetLast(uint8_t channel, float &ec, float &temperature, float &tdsPpm);

/** True while a non-blocking calibration is in progress. */
bool tdsCalibBusy();

/**
 * Start non-blocking conductivity calibration. Returns false if busy / bad args.
 * Call tdsPoll() from loop; when tdsCalibBusy() clears, tdsCalibTakeResult() has the outcome.
 */
bool tdsCalibStartConductivity(uint8_t channel, float referenceEC_usPerCm);

/** Start non-blocking NTC temperature calibration. */
bool tdsCalibStartTemperature(uint8_t channel, float referenceTempC);

/** If a calib just finished, copies ok into *okOut and returns true once. */
bool tdsCalibTakeResult(bool *okOut);

// Legacy sync API kept for tests — prefer start + poll in production path.
bool tdsRead(uint8_t channel, float &ec, float &temperature, float &tdsPpm);
bool tdsCalibrateConductivity(uint8_t channel, float referenceEC_usPerCm);
bool tdsCalibrateTemperature(uint8_t channel, float referenceTempC);

#endif // TDS_SENSOR_H
