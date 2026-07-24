#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>

#define EVENT_LOG_CAP 16
#define EVENT_MSG_LEN 48

struct EventLogEntry {
  char msg[EVENT_MSG_LEN];
  uint32_t millisStamp;  // wall-clock later (RTC/NTP)
};

void eventLogInit();
void eventLogAdd(const char *msg);
uint8_t eventLogCount();
EventLogEntry eventLogGet(uint8_t newestIndex);

#endif
