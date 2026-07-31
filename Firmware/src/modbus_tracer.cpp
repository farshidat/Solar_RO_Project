#include "modbus_tracer.h"
#include "config.h"

static uint32_t lastPollMs = 0;
static bool online = false;

void modbusTracerInit() {
  lastPollMs = 0;
  online = false;
#if !BENCH_SIMULATION_MODE
  // Production: HardwareSerial(1) on RS485_RX/TX — deferred until hardware installed.
#endif
}

void modbusTracerPoll() {
  const uint32_t now = millis();
  if ((now - lastPollMs) < MODBUS_POLL_MS) return;
  lastPollMs = now;

#if BENCH_SIMULATION_MODE
  (void)online;
  return;
#else
  // Phase 2: Tracer BN holding registers (Vsolar / SoC) via Modbus RTU.
  online = false;
#endif
}

bool modbusTracerOnline() { return online; }
