#include <Arduino.h>

// ===== فاز ۲ - تست ایزوله ماژول TDS دو کاناله (UART) =====
// منبع پروتکل: دیتاشیت اختصاصی ماژول (TDS 单路双路模块说明 v1.0)

#define TDS_RX_PIN 16   // ESP32 RX2  <-  TXD ماژول
#define TDS_TX_PIN 17   // ESP32 TX2  ->  RXD ماژول
#define TDS_BAUD   9600

HardwareSerial TDSSerial(2); // UART2

const uint8_t FRAME_HEADER = 0x55;
const uint8_t CMD_GET_CONDUCTIVITY = 0x05;
const uint8_t RESP_CONDUCTIVITY    = 0x85;

// ساخت فریم درخواست هدایت‌الکتریکی+دما برای یک کانال (1 یا 2)
uint8_t buildGetConductivityFrame(uint8_t channel, uint8_t *frame) {
  frame[0] = FRAME_HEADER;
  frame[1] = 0x07;               // Length = 1(length)+1(cmd)+4(data)+1(checksum)
  frame[2] = CMD_GET_CONDUCTIVITY;
  frame[3] = channel;            // Byte4: شماره کانال
  frame[4] = 0x00;
  frame[5] = 0x00;
  frame[6] = 0x00;
  uint8_t sum = 0;
  for (int i = 0; i < 7; i++) sum += frame[i];
  frame[7] = sum;                // Checksum: مجموع هدر تا انتهای داده
  return 8;
}

// خواندن یک فریم پاسخ ۱۱ بایتی با هم‌گام‌سازی روی بایت هدر 0x55
bool readResponse(uint8_t *buf, uint8_t expectedLen, uint32_t timeoutMs = 300) {
  uint32_t start = millis();
  uint8_t idx = 0;
  while (millis() - start < timeoutMs) {
    if (TDSSerial.available()) {
      buf[idx++] = TDSSerial.read();
      if (idx == 1 && buf[0] != FRAME_HEADER) { idx = 0; continue; }
      if (idx >= expectedLen) return true;
    }
  }
  return false;
}

// درخواست و دریافت هدایت‌الکتریکی (us/cm) و دما (°C) یک کانال
bool requestChannel(uint8_t channel, float &conductivity, float &temperature) {
  uint8_t reqFrame[8];
  uint8_t reqLen = buildGetConductivityFrame(channel, reqFrame);

  while (TDSSerial.available()) TDSSerial.read(); // پاکسازی بافر قدیمی

  TDSSerial.write(reqFrame, reqLen);

  uint8_t resp[11];
  if (!readResponse(resp, 11)) {
    Serial.printf("کانال %d: پاسخی دریافت نشد (Timeout)\n", channel);
    return false;
  }

  uint8_t sum = 0;
  for (int i = 0; i < 10; i++) sum += resp[i];
  if (sum != resp[10]) {
    Serial.println("خطا: چک‌سام فریم پاسخ نامعتبر است.");
    return false;
  }
  if (resp[2] != RESP_CONDUCTIVITY) {
    Serial.println("خطا: کد فرمان پاسخ نامعتبر است.");
    return false;
  }

  uint16_t condRaw = (resp[4] << 8) | resp[5]; // ×10
  uint16_t tempRaw = (resp[6] << 8) | resp[7]; // ×10
  conductivity = condRaw / 10.0f;
  temperature  = tempRaw / 10.0f;
  return true;
}

void setup() {
  Serial.begin(115200);
  TDSSerial.begin(TDS_BAUD, SERIAL_8N1, TDS_RX_PIN, TDS_TX_PIN);
  delay(500);
  Serial.println("=== فاز ۲: تست ماژول TDS دو کاناله ===");
}

void loop() {
  float cond1, temp1, cond2, temp2;

  if (requestChannel(0x01, cond1, temp1)) {
    float tds1 = cond1 / 2.0f; // TDS = EC / 2 (طبق دیتاشیت)
    Serial.printf("کانال ۱ (ورودی)  | دما: %.1f C | EC: %.1f us/cm | TDS: %.1f ppm\n",
                  temp1, cond1, tds1);
  }

  if (requestChannel(0x02, cond2, temp2)) {
    float tds2 = cond2 / 2.0f;
    Serial.printf("کانال ۲ (خروجی)  | دما: %.1f C | EC: %.1f us/cm | TDS: %.1f ppm\n",
                  temp2, cond2, tds2);
  }

  Serial.println("--------------------------------------------------");
  delay(2000);
}
