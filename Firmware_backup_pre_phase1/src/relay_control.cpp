#include <Arduino.h>
#include "config.h"
#include "relay_control.h"

#ifdef RELAY_ACTIVE_LOW
  #define RELAY_ON_LEVEL  LOW
  #define RELAY_OFF_LEVEL HIGH
#else
  #define RELAY_ON_LEVEL  HIGH
  #define RELAY_OFF_LEVEL LOW
#endif

static bool pumpState = false;
static bool uvState = false;
static bool rawPumpState = false;

static void relayWrite(uint8_t pin, bool on) {
  digitalWrite(pin, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

void relayInit() {
  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(RELAY_UV_PIN, OUTPUT);
  pinMode(RELAY_SENSOR_POWER_PIN, OUTPUT);
  pinMode(RELAY_RAW_PUMP_PIN, OUTPUT);

  relayWrite(RELAY_PUMP_PIN, false);
  relayWrite(RELAY_UV_PIN, false);
  relayWrite(RELAY_SENSOR_POWER_PIN, false);
  relayWrite(RELAY_RAW_PUMP_PIN, false);
}

void pumpOn()  { relayWrite(RELAY_PUMP_PIN, true);  pumpState = true; }
void pumpOff() { relayWrite(RELAY_PUMP_PIN, false); pumpState = false; }
bool pumpIsOn() { return pumpState; }

void uvOn()  { relayWrite(RELAY_UV_PIN, true);  uvState = true; }
void uvOff() { relayWrite(RELAY_UV_PIN, false); uvState = false; }
bool uvIsOn() { return uvState; }

void rawPumpOn()  { relayWrite(RELAY_RAW_PUMP_PIN, true);  rawPumpState = true; }
void rawPumpOff() { relayWrite(RELAY_RAW_PUMP_PIN, false); rawPumpState = false; }
bool rawPumpIsOn() { return rawPumpState; }

void sensorPowerOn()  { relayWrite(RELAY_SENSOR_POWER_PIN, true); }
void sensorPowerOff() { relayWrite(RELAY_SENSOR_POWER_PIN, false); }
