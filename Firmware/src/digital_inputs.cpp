#include "digital_inputs.h"
#include "config.h"

struct DebouncedPin {
  uint8_t pin;
  bool stableLevel;
  bool lastRaw;
  uint32_t lastChangeMs;
};

static DebouncedPin pressurePin;
static DebouncedPin floatPin;
static DebouncedPin leakPin;
static DigitalInputState state = {false, false, false};

static void initPin(DebouncedPin &p, uint8_t pin, int mode) {
  pinMode(pin, mode);
  p.pin = pin;
  p.lastRaw = digitalRead(pin);
  p.stableLevel = p.lastRaw;
  p.lastChangeMs = millis();
}

static void updatePin(DebouncedPin &p) {
  bool raw = digitalRead(p.pin);
  if (raw != p.lastRaw) {
    p.lastRaw = raw;
    p.lastChangeMs = millis();
  } else if ((millis() - p.lastChangeMs) >= INPUT_DEBOUNCE_MS) {
    p.stableLevel = raw;
  }
}

void digitalInputsInit() {
  // Float and leak: idle pulled high; pressure also pull-up for open-contact idle
  initPin(pressurePin, PRESSURE_SWITCH_PIN, INPUT_PULLUP);
  initPin(floatPin, FLOAT_SWITCH_PIN, INPUT_PULLUP);
  initPin(leakPin, LEAK_SENSOR_PIN, INPUT_PULLUP);
  digitalInputsUpdate();
}

void digitalInputsUpdate() {
  updatePin(pressurePin);
  updatePin(floatPin);
  updatePin(leakPin);

  // HIGH = pressure > 2 bar
  state.pressureOk = pressurePin.stableLevel;
  // HIGH = tank full, LOW = tank low
  state.tankFull = floatPin.stableLevel;
  // Active LOW = leak
  state.leakDetected = !leakPin.stableLevel;
}

DigitalInputState digitalInputsGet() {
  return state;
}
