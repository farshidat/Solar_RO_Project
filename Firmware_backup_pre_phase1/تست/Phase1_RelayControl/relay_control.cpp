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

static void relayWrite(uint8_t pin, bool on) {
  digitalWrite(pin, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

void relayInit() {
  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(RELAY_UV_PIN, OUTPUT);
  pinMode(RELAY_SENSOR_POWER_PIN, OUTPUT);
  pinMode(RELAY_SPARE_PIN, OUTPUT);

  relayWrite(RELAY_PUMP_PIN, false);
  relayWrite(RELAY_UV_PIN, false);
  relayWrite(RELAY_SENSOR_POWER_PIN, false);
  relayWrite(RELAY_SPARE_PIN, false);
}

void pumpOn()  { relayWrite(RELAY_PUMP_PIN, true); }
void pumpOff() { relayWrite(RELAY_PUMP_PIN, false); }

void uvOn()  { relayWrite(RELAY_UV_PIN, true); }
void uvOff() { relayWrite(RELAY_UV_PIN, false); }

void sensorPowerOn()  { relayWrite(RELAY_SENSOR_POWER_PIN, true); }
void sensorPowerOff() { relayWrite(RELAY_SENSOR_POWER_PIN, false); }
