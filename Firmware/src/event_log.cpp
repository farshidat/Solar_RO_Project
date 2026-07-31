#include "event_log.h"
#include <Preferences.h>
#include <string.h>
#include <time.h>

static EventLogEntry nvsRing[EVENT_NVS_CAP];
static uint8_t nvsHead = 0;
static uint8_t nvsCount = 0;

static uint16_t generation = 0;
static Preferences prefs;

// Per-code cumulative counters — NVS key "kXXXX" (avoid clashing with ring "c0"…)
static uint16_t counterFor(const char *code) {
  if (!code || !code[0]) return 0;
  char key[6];
  snprintf(key, sizeof(key), "k%.4s", code);
  prefs.begin("evtlog", true);
  uint16_t v = prefs.getUShort(key, 0);
  prefs.end();
  return v;
}

static uint16_t bumpCounter(const char *code) {
  if (!code || !code[0]) return 0;
  char key[6];
  snprintf(key, sizeof(key), "k%.4s", code);
  prefs.begin("evtlog", false);
  uint16_t v = (uint16_t)(prefs.getUShort(key, 0) + 1);
  if (v == 0) v = 1;
  prefs.putUShort(key, v);
  prefs.end();
  return v;
}

bool eventLogIsPersistentCode(const char *code) {
  (void)code;
  return true;  // all codes persist to NVS
}

static uint32_t wallEpochOrZero() {
  time_t t = time(nullptr);
  return (t > 1700000000L) ? (uint32_t)t : 0;
}

static void fillEntry(EventLogEntry &e, const char *code, uint16_t counter) {
  memset(&e, 0, sizeof(e));
  strncpy(e.code, code, EVENT_CODE_LEN - 1);
  e.millisStamp = millis();
  e.epochStamp = wallEpochOrZero();
  e.counter = counter;
}

static void saveNvsRing() {
  prefs.begin("evtlog", false);
  prefs.putUChar("n", nvsCount);
  prefs.putUChar("h", nvsHead);
  for (uint8_t i = 0; i < EVENT_NVS_CAP; i++) {
    char ck[8], ek[8], tk[8], mk[8];
    snprintf(ck, sizeof(ck), "c%u", (unsigned)i);
    snprintf(ek, sizeof(ek), "e%u", (unsigned)i);
    snprintf(tk, sizeof(tk), "t%u", (unsigned)i);
    snprintf(mk, sizeof(mk), "m%u", (unsigned)i);
    prefs.putString(ck, nvsRing[i].code);
    prefs.putULong(ek, nvsRing[i].epochStamp);
    prefs.putUShort(tk, nvsRing[i].counter);
    prefs.putULong(mk, nvsRing[i].millisStamp);
  }
  prefs.end();
}

static void loadNvsRing() {
  prefs.begin("evtlog", true);
  nvsCount = prefs.getUChar("n", 0);
  nvsHead = prefs.getUChar("h", 0);
  if (nvsCount > EVENT_NVS_CAP) nvsCount = EVENT_NVS_CAP;
  if (nvsHead >= EVENT_NVS_CAP) nvsHead = 0;
  for (uint8_t i = 0; i < EVENT_NVS_CAP; i++) {
    char ck[8], ek[8], tk[8], mk[8];
    snprintf(ck, sizeof(ck), "c%u", (unsigned)i);
    snprintf(ek, sizeof(ek), "e%u", (unsigned)i);
    snprintf(tk, sizeof(tk), "t%u", (unsigned)i);
    snprintf(mk, sizeof(mk), "m%u", (unsigned)i);
    char codeBuf[EVENT_CODE_LEN] = {0};
    prefs.getString(ck, codeBuf, sizeof(codeBuf));
    memset(&nvsRing[i], 0, sizeof(nvsRing[i]));
    if (codeBuf[0]) {
      strncpy(nvsRing[i].code, codeBuf, EVENT_CODE_LEN - 1);
      nvsRing[i].epochStamp = prefs.getULong(ek, 0);
      nvsRing[i].counter = prefs.getUShort(tk, 0);
      nvsRing[i].millisStamp = prefs.getULong(mk, 0);
    }
  }
  prefs.end();
}

static void pushNvs(const EventLogEntry &e) {
  nvsRing[nvsHead] = e;
  nvsHead = (uint8_t)((nvsHead + 1) % EVENT_NVS_CAP);
  if (nvsCount < EVENT_NVS_CAP) nvsCount++;
  saveNvsRing();
  generation++;
}

void eventLogInit() {
  nvsHead = 0;
  nvsCount = 0;
  generation = 0;
  memset(nvsRing, 0, sizeof(nvsRing));
  loadNvsRing();
}

void eventLogEmit(const char *code) {
  if (!code || !code[0]) return;
  uint16_t c = bumpCounter(code);
  EventLogEntry e;
  fillEntry(e, code, c);
  pushNvs(e);
}

void eventLogAdd(const char *msg) {
  if (!msg) return;
  size_t n = strlen(msg);
  if (n >= 4 && n < EVENT_CODE_LEN &&
      (msg[0] == 'E' || msg[0] == 'W' || msg[0] == 'O' || msg[0] == 'L')) {
    eventLogEmit(msg);
  }
}

void eventLogClearRam() {
  // No RAM history — all events live in NVS.
}

void eventLogClearPersistent() {
  nvsHead = 0;
  nvsCount = 0;
  memset(nvsRing, 0, sizeof(nvsRing));
  prefs.begin("evtlog", false);
  prefs.clear();
  prefs.end();
  generation++;
}

uint8_t eventLogCount() { return nvsCount; }
uint16_t eventLogGeneration() { return generation; }
uint16_t eventLogCodeCounter(const char *code) { return counterFor(code); }

EventLogEntry eventLogGet(uint8_t newestIndex) {
  EventLogEntry empty;
  memset(&empty, 0, sizeof(empty));
  if (newestIndex >= nvsCount) return empty;
  int idx = (int)nvsHead - 1 - (int)newestIndex;
  while (idx < 0) idx += EVENT_NVS_CAP;
  return nvsRing[idx];
}
