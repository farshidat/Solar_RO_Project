#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>
#include <string.h>
#include "event_codes.h"

struct EventLogEntry {
  char code[EVENT_CODE_LEN];
  uint32_t millisStamp;
  uint32_t epochStamp;  // Unix sec when clock synced; 0 = unknown
  uint16_t counter;     // cumulative emit count for this code at log time
};

void eventLogInit();

/** Emit event by short code — always persisted to NVS ring. */
void eventLogEmit(const char *code);

/** Alias — treats msg as a code if it matches E/W/O/L pattern. */
void eventLogAdd(const char *msg);

/** No-op kept for call-site compatibility (all history is NVS). */
void eventLogClearRam();

/** Clear NVS ring + per-code counters (system reset). */
void eventLogClearPersistent();

uint8_t eventLogCount();
uint16_t eventLogGeneration();

/** newestIndex 0 = most recent */
EventLogEntry eventLogGet(uint8_t newestIndex);

/** Always true — every code is NVS-backed (API compat). */
bool eventLogIsPersistentCode(const char *code);
uint16_t eventLogCodeCounter(const char *code);

// --- Deprecated aliases (map onto NVS ring) ---
inline uint8_t eventLogRamCount() { return 0; }
inline uint8_t eventLogPersistentCount() { return eventLogCount(); }
inline EventLogEntry eventLogRamGet(uint8_t) {
  EventLogEntry e;
  memset(&e, 0, sizeof(e));
  return e;
}
inline EventLogEntry eventLogPersistentGet(uint8_t i) { return eventLogGet(i); }

#endif
