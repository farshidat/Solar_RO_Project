#ifndef CONFIG_H
#define CONFIG_H

// ===== تنظیمات سریال =====
#define SERIAL_BAUD_RATE 115200

// ===== نگاشت پین رله‌ها (تست‌شده روی سخت‌افزار واقعی - برد ۴ رله ایرانیک) =====
#define RELAY_PUMP_PIN           4   // پمپ فشار بالا
#define RELAY_UV_PIN              2   // لامپ UV
#define RELAY_SENSOR_POWER_PIN   13  // قطع تغذیه سنسورها (Deep Sleep)
#define RELAY_RAW_PUMP_PIN       12  // پمپ آب خام (انتقال به مخزن مرتفع بالادست)

// ===== پولاریتی رله‌ها =====
// روی برد واقعی تست شد: رله‌ها Active HIGH هستند.
// #define RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_HIGH

// ===== ماژول TDS دو کاناله (UART2) =====
#define TDS_RX_PIN   16   // ESP32 RX2  <-  TXD ماژول
#define TDS_TX_PIN   17   // ESP32 TX2  ->  RXD ماژول
#define TDS_UART_BAUD 9600

// ===== Access Point و وب‌اپ محلی =====
#define WIFI_AP_SSID     "SolarRO"
#define WIFI_AP_PASSWORD "11223344"

#endif // CONFIG_H
