#include "app_state.h"
#include <string.h>

static AppSensors g;

void appStateInit() {
  memset(&g, 0, sizeof(g));
}

void appStateUpdateSensors(const AppSensors &s) { g = s; }

AppSensors appStateSensors() { return g; }
