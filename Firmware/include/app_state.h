#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>

struct AppSensors {
  bool pressureOk;       // Scenario A digital switch
  bool tankFull;
  bool leak;
  float tankPressureBar; // Scenario B (bench pot / future transducer)
  float tds1Ppm;
  float tds2Ppm;
  float temp1C;
  float temp2C;
  float ec1;
  float ec2;
  bool tds1Valid;
  bool tds2Valid;
  float vSolar;
  float socPercent;
};

void appStateInit();
void appStateUpdateSensors(const AppSensors &s);
AppSensors appStateSensors();

#endif
