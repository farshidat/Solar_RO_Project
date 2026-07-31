#ifndef MODBUS_TRACER_H
#define MODBUS_TRACER_H

#include <Arduino.h>

/** Init UART1 RS485 path (no-op while BENCH_SIMULATION_MODE). */
void modbusTracerInit();

/**
 * Non-blocking Modbus poll — call every loop().
 * Interval gated internally to MODBUS_POLL_MS (1000 ms).
 * Production fill-in later; bench mode returns immediately.
 */
void modbusTracerPoll();

bool modbusTracerOnline();

#endif // MODBUS_TRACER_H
