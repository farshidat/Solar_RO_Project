#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>

// Shared live snapshot filled each loop before control modules run.
struct AppSensors {
  bool pressureOk;
  bool tankFull;      // product tank float: true = full
  bool leak;
  float tds1Ppm;
  float tds2Ppm;
  float temp1C;
  float temp2C;
  float ec1;
  float ec2;
  bool tds1Valid;
  bool tds2Valid;
  float vSolar;
};

void appStateInit();
void appStateUpdateSensors(const AppSensors &s);
AppSensors appStateSensors();

#endif // APP_STATE_H
