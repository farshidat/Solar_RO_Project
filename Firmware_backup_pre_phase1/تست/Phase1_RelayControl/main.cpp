#include <Arduino.h>
#include "config.h"
#include "relay_control.h"

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(500);

  Serial.println("========================================");
  Serial.println("Solar RO Water Treatment Controller");
  Serial.println("Current Phase: 1 - Relay Control");
  Serial.println("Boot OK");
  Serial.println("========================================");

  relayInit();
  Serial.println("Relays initialized (all OFF).");
}

void loop() {
  Serial.println("[Pump Relay] -> ON");
  pumpOn();
  delay(2000);
  Serial.println("[Pump Relay] -> OFF");
  pumpOff();
  delay(2000);

  Serial.println("[UV Relay] -> ON");
  uvOn();
  delay(2000);
  Serial.println("[UV Relay] -> OFF");
  uvOff();
  delay(2000);

  Serial.println("[Sensor Power Relay] -> ON");
  sensorPowerOn();
  delay(2000);
  Serial.println("[Sensor Power Relay] -> OFF");
  sensorPowerOff();
  delay(2000);
}
