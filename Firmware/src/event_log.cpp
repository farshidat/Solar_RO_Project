#include "event_log.h"
#include <Preferences.h>
#include <string.h>
#include <time.h>

static EventLogEntry ramRing[EVENT_RAM_CAP];
static uint8_t ramHead = 0;
static uint8_t ramCount = 0;

static EventLogEntry nvsRing[EVENT_NVS_CAP];
static uint8_t nvsHead = 0;
static uint8_t nvsCount = 0;

static uint16_t generation = 0;
static Preferences prefs;

// Per-code cumulative counters (persistent codes only) — NVS key "cXXXX"
static uint16_t counterFor(const char *code) {
  if (!code || !code[0]) return 0;
  char key[6];
  // "c" + up to 4 chars of code
  snprintf(key, sizeof(key), "c%.4s", code);
  prefs.begin("evtlog", true);
  uint16_t v = prefs.getUShort(key, 0);
  prefs.end();
  return v;
}

static uint16_t bumpCounter(const char *code) {
  if (!code || !code[0]) return 0;
  char key[6];
  snprintf(key, sizeof(key), "c%.4s", code);
  prefs.begin("evtlog", false);
  uint16_t v = (uint16_t)(prefs.getUShort(key, 0) + 1);
  if (v == 0) v = 1;
  prefs.putUShort(key, v);
  prefs.end();
  return v;
}

bool eventLogIsPersistentCode(const char *code) {
  if (!code) return false;
  return strcmp(code, CODE_E101) == 0 || strcmp(code, CODE_E102) == 0 ||
         strcmp(code, CODE_E103) == 0 || strcmp(code, CODE_E104) == 0 ||
         strcmp(code, CODE_E105) == 0 || strcmp(code, CODE_E106) == 0 ||
         strcmp(code, CODE_W201) == 0 || strcmp(code, CODE_W207) == 0 ||
         strcmp(code, CODE_L301) == 0;
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

static void pushRam(const EventLogEntry &e) {
  ramRing[ramHead] = e;
  ramHead = (uint8_t)((ramHead + 1) % EVENT_RAM_CAP);
  if (ramCount < EVENT_RAM_CAP) ramCount++;
  generation++;
}

static void saveNvsRing() {
  prefs.begin("evtlog", false);
  prefs.putUChar("n", nvsCount);
  prefs.putUChar("h", nvsHead);
  for (uint8_t i = 0; i < EVENT_NVS_CAP; i++) {
    char ck[8], ek[8], tk[8];
    snprintf(ck, sizeof(ck), "c%u", (unsigned)i);
    snprintf(ek, sizeof(ek), "e%u", (unsigned)i);
    snprintf(tk, sizeof(tk), "t%u", (unsigned)i);
    prefs.putString(ck, nvsRing[i].code);
    prefs.putULong(ek, nvsRing[i].epochStamp);
    prefs.putUShort(tk, nvsRing[i].counter);
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
    char ck[8], ek[8], tk[8];
    snprintf(ck, sizeof(ck), "c%u", (unsigned)i);
    snprintf(ek, sizeof(ek), "e%u", (unsigned)i);
    snprintf(tk, sizeof(tk), "t%u", (unsigned)i);
    String s = prefs.getString(ck, "");
    memset(&nvsRing[i], 0, sizeof(nvsRing[i]));
    if (s.length() > 0) {
      strncpy(nvsRing[i].code, s.c_str(), EVENT_CODE_LEN - 1);
      nvsRing[i].epochStamp = prefs.getULong(ek, 0);
      nvsRing[i].counter = prefs.getUShort(tk, 0);
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
  ramHead = 0;
  ramCount = 0;
  nvsHead = 0;
  nvsCount = 0;
  generation = 0;
  memset(ramRing, 0, sizeof(ramRing));
  memset(nvsRing, 0, sizeof(nvsRing));
  loadNvsRing();
}

void eventLogEmit(const char *code) {
  if (!code || !code[0]) return;

  if (eventLogIsPersistentCode(code)) {
    uint16_t c = bumpCounter(code);
    EventLogEntry e;
    fillEntry(e, code, c);
    pushNvs(e);
    pushRam(e);  // also mirror to RAM for live UI this session
  } else {
    EventLogEntry e;
    fillEntry(e, code, 0);
    pushRam(e);
  }
}

void eventLogAdd(const char *msg) {
  // Backward-compatible: if msg looks like a short code, emit it; else ignore
  if (!msg) return;
  size_t n = strlen(msg);
  if (n >= 4 && n < EVENT_CODE_LEN &&
      (msg[0] == 'E' || msg[0] == 'W' || msg[0] == 'O' || msg[0] == 'L')) {
    eventLogEmit(msg);
  }
}

void eventLogClearRam() {
  ramHead = 0;
  ramCount = 0;
  memset(ramRing, 0, sizeof(ramRing));
  generation++;
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

uint8_t eventLogRamCount() { return ramCount; }
uint8_t eventLogPersistentCount() { return nvsCount; }
uint16_t eventLogGeneration() { return generation; }

uint16_t eventLogCodeCounter(const char *code) { return counterFor(code); }

static EventLogEntry getFromRing(const EventLogEntry *ring, uint8_t head, uint8_t count,
                                 uint8_t newestIndex, uint8_t cap) {
  EventLogEntry empty;
  memset(&empty, 0, sizeof(empty));
  if (newestIndex >= count) return empty;
  int idx = (int)head - 1 - (int)newestIndex;
  while (idx < 0) idx += cap;
  return ring[idx];
}

EventLogEntry eventLogRamGet(uint8_t newestIndex) {
  return getFromRing(ramRing, ramHead, ramCount, newestIndex, EVENT_RAM_CAP);
}

EventLogEntry eventLogPersistentGet(uint8_t newestIndex) {
  return getFromRing(nvsRing, nvsHead, nvsCount, newestIndex, EVENT_NVS_CAP);
}
