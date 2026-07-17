#ifndef CONFIG_H
#define CONFIG_H

// ===== تنظیمات سریال =====
#define SERIAL_BAUD_RATE 115200

// ===== نگاشت پین رله‌ها (تست‌شده روی سخت‌افزار واقعی - برد ۴ رله ایرانیک) =====
#define RELAY_PUMP_PIN           4   // پمپ فشار بالا
#define RELAY_UV_PIN              2   // لامپ UV
#define RELAY_SENSOR_POWER_PIN   13  // قطع تغذیه سنسورها (Deep Sleep)
#define RELAY_SPARE_PIN          12  // رزرو / استفاده آینده

// ===== پولاریتی رله‌ها =====
// روی برد واقعی تست شد: رله‌ها Active HIGH هستند.
// #define RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_HIGH

#endif // CONFIG_H
