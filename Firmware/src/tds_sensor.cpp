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

static uint8_t computeChecksum(const uint8_t *frame, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum += frame[i];
  return sum;
}

// All request frames used here carry exactly 4 data bytes (Length = 0x07).
static uint8_t buildFrame4(uint8_t command, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t *frame) {
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

// All response frames used here are 11 bytes (Length = 0x0a). Resyncs on the
// header byte and validates the checksum before accepting a frame.
static bool readFrame11(uint8_t *buf, uint32_t timeoutMs) {
  uint32_t start = millis();
  uint8_t idx = 0;
  while (millis() - start < timeoutMs) {
    if (TDSSerial.available()) {
      buf[idx++] = TDSSerial.read();
      if (idx == 1 && buf[0] != FRAME_HEADER) { idx = 0; continue; }
      if (idx >= 11) {
        if (computeChecksum(buf, 10) != buf[10]) { idx = 0; continue; }
        return true;
      }
    }
  }
  return false;
}

static void sendFrame(const uint8_t *frame, uint8_t len) {
  while (TDSSerial.available()) TDSSerial.read(); // flush stale bytes
  TDSSerial.write(frame, len);
}

void tdsInit() {
  TDSSerial.begin(TDS_UART_BAUD, SERIAL_8N1, TDS_RX_PIN, TDS_TX_PIN);
}

bool tdsRead(uint8_t channel, float &ec, float &temperature, float &tdsPpm) {
  uint8_t frame[8];
  buildFrame4(CMD_GET_CONDUCTIVITY, channel, 0x00, 0x00, 0x00, frame);
  sendFrame(frame, 8);

  uint8_t resp[11];
  if (!readFrame11(resp, 300)) return false;
  if (resp[2] != RESP_CONDUCTIVITY) return false;

  uint16_t ecRaw   = (resp[4] << 8) | resp[5];
  uint16_t tempRaw = (resp[6] << 8) | resp[7];
  ec = ecRaw / 10.0f;
  temperature = tempRaw / 10.0f;
  tdsPpm = ec / 2.0f; // TDS = EC / 2 (per datasheet)
  return true;
}

bool tdsCalibrateConductivity(uint8_t channel, float referenceEC_usPerCm) {
  uint16_t concX10 = (uint16_t)(referenceEC_usPerCm * 10.0f + 0.5f);
  uint8_t frame[8];
  buildFrame4(CMD_SET_TDS_CALIB_MODE, channel, 0x00, (concX10 >> 8) & 0xFF, concX10 & 0xFF, frame);
  sendFrame(frame, 8);

  uint8_t ack[11];
  if (!readFrame11(ack, 500) || ack[2] != RESP_TDS_CALIB_ACK) return false;

  // Module calibrates asynchronously; poll until it reports "calibrated".
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    delay(500);
    uint8_t infoReq[8];
    buildFrame4(CMD_GET_TDS_CALIB_INFO, channel, 0x00, 0x00, 0x00, infoReq);
    sendFrame(infoReq, 8);

    uint8_t info[11];
    if (readFrame11(info, 300) && info[2] == RESP_TDS_CALIB_INFO && info[3] == 0x01) {
      return true;
    }
  }
  return false;
}

bool tdsCalibrateTemperature(uint8_t channel, float referenceTempC) {
  uint16_t tempX10 = (uint16_t)(referenceTempC * 10.0f + 0.5f);
  uint8_t frame[8];
  buildFrame4(CMD_SET_NTC_CALIB_MODE, channel, (tempX10 >> 8) & 0xFF, tempX10 & 0xFF, 0x00, frame);
  sendFrame(frame, 8);

  uint8_t ack[11];
  if (!readFrame11(ack, 500) || ack[2] != RESP_NTC_CALIB_ACK) return false;

  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    delay(500);
    uint8_t infoReq[8];
    buildFrame4(CMD_GET_NTC_CALIB_INFO, channel, 0x00, 0x00, 0x00, infoReq);
    sendFrame(infoReq, 8);

    uint8_t info[11];
    if (readFrame11(info, 300) && info[2] == RESP_NTC_CALIB_INFO && info[3] == 0x01) {
      return true;
    }
  }
  return false;
}
