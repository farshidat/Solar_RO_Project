#include "event_log.h"
#include <string.h>

static EventLogEntry ring[EVENT_LOG_CAP];
static uint8_t head = 0;
static uint8_t count = 0;

void eventLogInit() {
  head = 0;
  count = 0;
}

void eventLogAdd(const char *msg) {
  if (!msg) return;
  EventLogEntry &e = ring[head];
  strncpy(e.msg, msg, EVENT_MSG_LEN - 1);
  e.msg[EVENT_MSG_LEN - 1] = '\0';
  e.millisStamp = millis();
  head = (uint8_t)((head + 1) % EVENT_LOG_CAP);
  if (count < EVENT_LOG_CAP) count++;
}

uint8_t eventLogCount() { return count; }

EventLogEntry eventLogGet(uint8_t newestIndex) {
  EventLogEntry empty;
  memset(&empty, 0, sizeof(empty));
  if (newestIndex >= count) return empty;
  int idx = (int)head - 1 - (int)newestIndex;
  while (idx < 0) idx += EVENT_LOG_CAP;
  return ring[idx];
}
