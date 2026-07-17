#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

void relayInit();

void pumpOn();
void pumpOff();
bool pumpIsOn();

void uvOn();
void uvOff();
bool uvIsOn();

void rawPumpOn();
void rawPumpOff();
bool rawPumpIsOn();

void sensorPowerOn();
void sensorPowerOff();

#endif // RELAY_CONTROL_H
