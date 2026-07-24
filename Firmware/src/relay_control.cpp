#include "relay_control.h"
#include "config.h"

#ifdef RELAY_ACTIVE_LOW
  #define RELAY_ON_LEVEL  LOW
  #define RELAY_OFF_LEVEL HIGH
#else
  #define RELAY_ON_LEVEL  HIGH
  #define RELAY_OFF_LEVEL LOW
#endif

static bool r1 = false;
static bool r2 = false;
static bool r3 = false;
static bool r4 = false;
static bool sensorPwr = false;

static void relayWrite(uint8_t pin, bool on) {
  digitalWrite(pin, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

void relayInit() {
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  pinMode(RELAY4_PIN, OUTPUT);
  pinMode(SENSOR_POWER_MOSFET_PIN, OUTPUT);

  relay1Off();
  relay2Off();
  purificationOff();
  nightLightOff();
  sensorPowerOn();  // sensors powered in Phase 1
}

void relay1On()  { relayWrite(RELAY1_PIN, true);  r1 = true; }
void relay1Off() { relayWrite(RELAY1_PIN, false); r1 = false; }
bool relay1IsOn() { return r1; }

void relay2On()  { relayWrite(RELAY2_PIN, true);  r2 = true; }
void relay2Off() { relayWrite(RELAY2_PIN, false); r2 = false; }
bool relay2IsOn() { return r2; }

void purificationOn()  { relayWrite(RELAY3_PIN, true);  r3 = true; }
void purificationOff() { relayWrite(RELAY3_PIN, false); r3 = false; }
bool purificationIsOn() { return r3; }

void nightLightOn()  { relayWrite(RELAY4_PIN, true);  r4 = true; }
void nightLightOff() { relayWrite(RELAY4_PIN, false); r4 = false; }
bool nightLightIsOn() { return r4; }

void sensorPowerOn() {
#if SENSOR_POWER_ACTIVE_LOW
  digitalWrite(SENSOR_POWER_MOSFET_PIN, LOW);
#else
  digitalWrite(SENSOR_POWER_MOSFET_PIN, HIGH);
#endif
  sensorPwr = true;
}

void sensorPowerOff() {
#if SENSOR_POWER_ACTIVE_LOW
  digitalWrite(SENSOR_POWER_MOSFET_PIN, HIGH);
#else
  digitalWrite(SENSOR_POWER_MOSFET_PIN, LOW);
#endif
  sensorPwr = false;
}

bool sensorPowerIsOn() { return sensorPwr; }
