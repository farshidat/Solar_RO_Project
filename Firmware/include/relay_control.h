#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <Arduino.h>

void relayInit();

// Relay 1 — Scenario A: inlet close when ON / Scenario B: raw pump when ON
void relay1On();
void relay1Off();
bool relay1IsOn();

// Relay 2 — Drain valve
void relay2On();
void relay2Off();
bool relay2IsOn();

// Relay 3 — Purification pump + UV (always together)
void purificationOn();
void purificationOff();
bool purificationIsOn();

// Relay 4 — Night lighting
void nightLightOn();
void nightLightOff();
bool nightLightIsOn();

// P-MOSFET sensor rail (Active Low enable)
void sensorPowerOn();
void sensorPowerOff();
bool sensorPowerIsOn();

// Legacy aliases used by older web commands (map to new actuators)
inline void pumpOn()  { purificationOn(); }
inline void pumpOff() { purificationOff(); }
inline bool pumpIsOn() { return purificationIsOn(); }
inline void uvOn()  { purificationOn(); }
inline void uvOff() { purificationOff(); }
inline bool uvIsOn() { return purificationIsOn(); }
inline void rawPumpOn()  { relay1On(); }
inline void rawPumpOff() { relay1Off(); }
inline bool rawPumpIsOn() { return relay1IsOn(); }

#endif // RELAY_CONTROL_H
