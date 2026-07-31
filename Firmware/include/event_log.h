#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>
#include "event_codes.h"

struct EventLogEntry {
  char code[EVENT_CODE_LEN];
  uint32_t millisStamp;
  uint32_t epochStamp;  // Unix sec when clock synced; 0 = unknown
  uint16_t counter;     // cumulative emit count for this code at log time
};

void eventLogInit();

/** Emit event by short code. Persistent codes → NVS ring; others → RAM only. */
void eventLogEmit(const char *code);

/** Alias kept for gradual migration — treats msg as a code if it matches E/W/O/L pattern. */
void eventLogAdd(const char *msg);

void eventLogClearRam();
void eventLogClearPersistent();  // NVS ring + per-code counters (system reset)

uint8_t eventLogRamCount();
uint8_t eventLogPersistentCount();
uint16_t eventLogGeneration();

EventLogEntry eventLogRamGet(uint8_t newestIndex);
EventLogEntry eventLogPersistentGet(uint8_t newestIndex);

bool eventLogIsPersistentCode(const char *code);
uint16_t eventLogCodeCounter(const char *code);

#endif
