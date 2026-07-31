#include "event_log.h"
#include <string.h>
#include <time.h>

static EventLogEntry ring[EVENT_LOG_CAP];
static uint8_t head = 0;
static uint8_t count = 0;
static uint16_t generation = 0;

void eventLogInit() {
  head = 0;
  count = 0;
  generation = 0;
}

void eventLogAdd(const char *msg) {
  if (!msg) return;
  EventLogEntry &e = ring[head];
  strncpy(e.msg, msg, EVENT_MSG_LEN - 1);
  e.msg[EVENT_MSG_LEN - 1] = '\0';
  e.millisStamp = millis();
  {
    time_t t = time(nullptr);
    e.epochStamp = (t > 1700000000L) ? (uint32_t)t : 0;
  }
  head = (uint8_t)((head + 1) % EVENT_LOG_CAP);
  if (count < EVENT_LOG_CAP) count++;
  generation++;
}

uint8_t eventLogCount() { return count; }
uint16_t eventLogGeneration() { return generation; }

EventLogEntry eventLogGet(uint8_t newestIndex) {
  EventLogEntry empty;
  memset(&empty, 0, sizeof(empty));
  if (newestIndex >= count) return empty;
  int idx = (int)head - 1 - (int)newestIndex;
  while (idx < 0) idx += EVENT_LOG_CAP;
  return ring[idx];
}
