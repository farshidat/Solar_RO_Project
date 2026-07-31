#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>

#define EVENT_LOG_CAP 16
#define EVENT_MSG_LEN 64

struct EventLogEntry {
  char msg[EVENT_MSG_LEN];
  uint32_t millisStamp;
  uint32_t epochStamp;  // Unix sec when clock synced; 0 = unknown (UI shows --)
};

void eventLogInit();
void eventLogAdd(const char *msg);
uint8_t eventLogCount();
uint16_t eventLogGeneration();
EventLogEntry eventLogGet(uint8_t newestIndex);

#endif
