#include "tds_sensor.h"
#include "config.h"

// Protocol source: vendor datasheet (TDS Dual-Channel Module v1.0)

static HardwareSerial TDSSerial(2); // UART2

static const uint8_t FRAME_HEADER = 0x55;

static const uint8_t CMD_GET_TDS_CALIB_INFO  = 0x01;
static const uint8_t CMD_GET_NTC_CALIB_INFO  = 0x02;
static const uint8_t CMD_SET_TDS_CALIB_MODE  = 0x03;
static const uint8_t CMD_SET_NTC_CALIB_MODE  = 0x04;
static const uint8_t CMD_GET_CONDUCTIVITY    = 0x05;

static const uint8_t RESP_TDS_CALIB_INFO = 0x81;
static const uint8_t RESP_NTC_CALIB_INFO = 0x82;
static const uint8_t RESP_TDS_CALIB_ACK  = 0x83;
static const uint8_t RESP_NTC_CALIB_ACK  = 0x84;
static const uint8_t RESP_CONDUCTIVITY   = 0x85;

enum TdsState : uint8_t {
  TDS_IDLE = 0,
  TDS_WAIT_READ,
  TDS_CALIB_WAIT_ACK,
  TDS_CALIB_PAUSE,
  TDS_CALIB_WAIT_INFO,
};

static TdsState state = TDS_IDLE;
static uint8_t activeChannel = 1;
static uint8_t nextReadChannel = 1;
static uint8_t rxBuf[11];
static uint8_t rxIdx = 0;
static uint32_t waitStartMs = 0;
static uint32_t lastPollMs = 0;
static uint32_t pauseUntilMs = 0;
static uint8_t calibAttempts = 0;
static uint8_t calibExpectAck = 0;
static uint8_t calibExpectInfo = 0;
static uint8_t calibInfoCmd = 0;
static bool calibDonePending = false;
static bool calibOk = false;
static uint8_t failStreak = 0;

struct TdsSample {
  bool valid;
  float ec;
  float tempC;
  float tdsPpm;
};
static TdsSample ch1 = {false, 0, 0, 0};
static TdsSample ch2 = {false, 0, 0, 0};

static uint8_t computeChecksum(const uint8_t *frame, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum += frame[i];
  return sum;
}

static uint8_t buildFrame4(uint8_t command, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                           uint8_t *frame) {
  frame[0] = FRAME_HEADER;
  frame[1] = 0x07;
  frame[2] = command;
  frame[3] = d0;
  frame[4] = d1;
  frame[5] = d2;
  frame[6] = d3;
  frame[7] = computeChecksum(frame, 7);
  return 8;
}

static void sendFrame(const uint8_t *frame, uint8_t len) {
  while (TDSSerial.available()) (void)TDSSerial.read();
  TDSSerial.write(frame, len);
}

static void beginWait(TdsState next) {
  rxIdx = 0;
  waitStartMs = millis();
  state = next;
}

static bool feedRxByte(uint8_t b) {
  if (rxIdx == 0 && b != FRAME_HEADER) return false;
  if (rxIdx >= 11) return false;
  rxBuf[rxIdx++] = b;
  if (rxIdx < 11) return false;
  if (computeChecksum(rxBuf, 10) != rxBuf[10]) {
    rxIdx = 0;
    return false;
  }
  return true;
}

/** Drain UART without blocking; returns true when a full valid 11-byte frame is ready. */
static bool tryTakeFrame() {
  while (TDSSerial.available()) {
    if (feedRxByte((uint8_t)TDSSerial.read())) return true;
  }
  return false;
}

static bool waitTimedOut(uint32_t timeoutMs) {
  return (millis() - waitStartMs) >= timeoutMs;
}

static void storeSample(uint8_t channel, float ec, float temp, float tds) {
  TdsSample *s = (channel == 1) ? &ch1 : &ch2;
  s->valid = true;
  s->ec = ec;
  s->tempC = temp;
  s->tdsPpm = tds;
}

static void finishCalib(bool ok) {
  calibOk = ok;
  calibDonePending = true;
  state = TDS_IDLE;
}

static void startReadRequest(uint8_t channel) {
  activeChannel = channel;
  uint8_t frame[8];
  buildFrame4(CMD_GET_CONDUCTIVITY, channel, 0x00, 0x00, 0x00, frame);
  sendFrame(frame, 8);
  beginWait(TDS_WAIT_READ);
}

void tdsInit() {
  TDSSerial.begin(TDS_UART_BAUD, SERIAL_8N1, TDS_RX_PIN, TDS_TX_PIN);
  state = TDS_IDLE;
  lastPollMs = 0;
  nextReadChannel = 1;
  ch1.valid = ch2.valid = false;
  calibDonePending = false;
  failStreak = 0;
}

uint8_t tdsFailStreak() { return failStreak; }

bool tdsCalibBusy() {
  return state == TDS_CALIB_WAIT_ACK || state == TDS_CALIB_PAUSE || state == TDS_CALIB_WAIT_INFO;
}

bool tdsCalibTakeResult(bool *okOut) {
  if (!calibDonePending) return false;
  calibDonePending = false;
  if (okOut) *okOut = calibOk;
  return true;
}

bool tdsGetLast(uint8_t channel, float &ec, float &temperature, float &tdsPpm) {
  const TdsSample &s = (channel == 1) ? ch1 : ch2;
  if (!s.valid) return false;
  ec = s.ec;
  temperature = s.tempC;
  tdsPpm = s.tdsPpm;
  return true;
}

bool tdsPoll() {
  bool completed = false;
  const uint32_t now = millis();

  switch (state) {
    case TDS_IDLE:
      if ((now - lastPollMs) < TDS_POLL_MS) break;
      lastPollMs = now;
      startReadRequest(nextReadChannel);
      break;

    case TDS_WAIT_READ:
      if (tryTakeFrame()) {
        if (rxBuf[2] == RESP_CONDUCTIVITY) {
          const uint16_t ecRaw = (uint16_t)((rxBuf[4] << 8) | rxBuf[5]);
          const uint16_t tempRaw = (uint16_t)((rxBuf[6] << 8) | rxBuf[7]);
          const float ec = ecRaw / 10.0f;
          const float temp = tempRaw / 10.0f;
          const float tds = ec / 2.0f;
          storeSample(activeChannel, ec, temp, tds);
          failStreak = 0;
          completed = true;
        } else if (failStreak < 250) {
          failStreak++;
        }
        nextReadChannel = (activeChannel == 1) ? 2 : 1;
        state = TDS_IDLE;
      } else if (waitTimedOut(TDS_READ_TIMEOUT_MS)) {
        if (failStreak < 250) failStreak++;
        nextReadChannel = (activeChannel == 1) ? 2 : 1;
        state = TDS_IDLE;
      }
      break;

    case TDS_CALIB_WAIT_ACK:
      if (tryTakeFrame()) {
        if (rxBuf[2] == calibExpectAck) {
          calibAttempts = 0;
          pauseUntilMs = now + 500UL;
          state = TDS_CALIB_PAUSE;
        } else {
          finishCalib(false);
        }
      } else if (waitTimedOut(500)) {
        finishCalib(false);
      }
      break;

    case TDS_CALIB_PAUSE:
      if ((int32_t)(now - pauseUntilMs) < 0) break;
      {
        uint8_t infoReq[8];
        buildFrame4(calibInfoCmd, activeChannel, 0x00, 0x00, 0x00, infoReq);
        sendFrame(infoReq, 8);
        beginWait(TDS_CALIB_WAIT_INFO);
      }
      break;

    case TDS_CALIB_WAIT_INFO:
      if (tryTakeFrame()) {
        if (rxBuf[2] == calibExpectInfo && rxBuf[3] == 0x01) {
          finishCalib(true);
        } else if (++calibAttempts >= 5) {
          finishCalib(false);
        } else {
          pauseUntilMs = now + 500UL;
          state = TDS_CALIB_PAUSE;
        }
      } else if (waitTimedOut(300)) {
        if (++calibAttempts >= 5) finishCalib(false);
        else {
          pauseUntilMs = now + 500UL;
          state = TDS_CALIB_PAUSE;
        }
      }
      break;
  }

  return completed;
}

bool tdsCalibStartConductivity(uint8_t channel, float referenceEC_usPerCm) {
  if (tdsCalibBusy() || channel < 1 || channel > 2) return false;
  state = TDS_IDLE;  // abort in-flight read if any
  const uint16_t concX10 = (uint16_t)(referenceEC_usPerCm * 10.0f + 0.5f);
  uint8_t frame[8];
  buildFrame4(CMD_SET_TDS_CALIB_MODE, channel, 0x00, (concX10 >> 8) & 0xFF, concX10 & 0xFF, frame);
  activeChannel = channel;
  calibExpectAck = RESP_TDS_CALIB_ACK;
  calibExpectInfo = RESP_TDS_CALIB_INFO;
  calibInfoCmd = CMD_GET_TDS_CALIB_INFO;
  sendFrame(frame, 8);
  beginWait(TDS_CALIB_WAIT_ACK);
  return true;
}

bool tdsCalibStartTemperature(uint8_t channel, float referenceTempC) {
  if (tdsCalibBusy() || channel < 1 || channel > 2) return false;
  state = TDS_IDLE;
  const uint16_t tempX10 = (uint16_t)(referenceTempC * 10.0f + 0.5f);
  uint8_t frame[8];
  buildFrame4(CMD_SET_NTC_CALIB_MODE, channel, (tempX10 >> 8) & 0xFF, tempX10 & 0xFF, 0x00, frame);
  activeChannel = channel;
  calibExpectAck = RESP_NTC_CALIB_ACK;
  calibExpectInfo = RESP_NTC_CALIB_INFO;
  calibInfoCmd = CMD_GET_NTC_CALIB_INFO;
  sendFrame(frame, 8);
  beginWait(TDS_CALIB_WAIT_ACK);
  return true;
}

// --- Legacy sync helpers (blocking) — only for offline tests / rare paths ---

static bool readFrame11Blocking(uint8_t *buf, uint32_t timeoutMs) {
  const uint32_t start = millis();
  uint8_t idx = 0;
  while (millis() - start < timeoutMs) {
    if (TDSSerial.available()) {
      const uint8_t b = (uint8_t)TDSSerial.read();
      if (idx == 0 && b != FRAME_HEADER) continue;
      buf[idx++] = b;
      if (idx >= 11) {
        if (computeChecksum(buf, 10) != buf[10]) {
          idx = 0;
          continue;
        }
        return true;
      }
    } else {
      yield();
    }
  }
  return false;
}

bool tdsRead(uint8_t channel, float &ec, float &temperature, float &tdsPpm) {
  if (state != TDS_IDLE) return tdsGetLast(channel, ec, temperature, tdsPpm);
  uint8_t frame[8];
  buildFrame4(CMD_GET_CONDUCTIVITY, channel, 0x00, 0x00, 0x00, frame);
  sendFrame(frame, 8);
  uint8_t resp[11];
  if (!readFrame11Blocking(resp, TDS_READ_TIMEOUT_MS)) return false;
  if (resp[2] != RESP_CONDUCTIVITY) return false;
  const uint16_t ecRaw = (uint16_t)((resp[4] << 8) | resp[5]);
  const uint16_t tempRaw = (uint16_t)((resp[6] << 8) | resp[7]);
  ec = ecRaw / 10.0f;
  temperature = tempRaw / 10.0f;
  tdsPpm = ec / 2.0f;
  storeSample(channel, ec, temperature, tdsPpm);
  return true;
}

bool tdsCalibrateConductivity(uint8_t channel, float referenceEC_usPerCm) {
  if (!tdsCalibStartConductivity(channel, referenceEC_usPerCm)) return false;
  const uint32_t deadline = millis() + 4000UL;
  while (tdsCalibBusy() && (int32_t)(millis() - deadline) < 0) {
    tdsPoll();
    yield();
  }
  bool ok = false;
  if (!tdsCalibTakeResult(&ok)) return false;
  return ok;
}

bool tdsCalibrateTemperature(uint8_t channel, float referenceTempC) {
  if (!tdsCalibStartTemperature(channel, referenceTempC)) return false;
  const uint32_t deadline = millis() + 4000UL;
  while (tdsCalibBusy() && (int32_t)(millis() - deadline) < 0) {
    tdsPoll();
    yield();
  }
  bool ok = false;
  if (!tdsCalibTakeResult(&ok)) return false;
  return ok;
}
