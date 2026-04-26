#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================
// PIN MAPPING — DO NOT MODIFY (Hardware-locked)
// =============================================================

// I2C Pins (Shared: MAX30102 @ 0x57, MPU6050 @ 0x68)
#define I2C_SDA 8
#define I2C_SCL 9

// GSR Sensor (Analog — MUST be ADC1 for BT/WiFi compatibility)
#define GSR_PIN 4 // ADC1_CH3 on ESP32-S3

// SD Card (SPI)
#define SD_CS 10
#define SD_MOSI 11
#define SD_MISO 13
#define SD_SCK 12

// No hardware interrupts used; strictly polling.

// =============================================================
// SAMPLING CONFIGURATION
// =============================================================

#define SAMPLING_RATE_HZ 100
#define SAMPLING_PERIOD_US (1000000 / SAMPLING_RATE_HZ)

// =============================================================
// ALARM / WAKE WINDOW CONFIG
// =============================================================

#define WIFI_SSID "ProjectSomnus"
#define WIFI_PASS "Pass1234"
#define WIFI_TIMEOUT_MS 10000     // 10s wait for NTP sync
#define UTC_OFFSET_SEC -14400     // EDT is UTC-4
#define NTP_SERVER "pool.ntp.org"

#define WAKE_WINDOW_START_HR 6    // 6:00 AM (24-hour format)
#define WAKE_WINDOW_START_MIN 30  // 6:30 AM
#define WAKE_WINDOW_DUR_MINS 30   // Window ends at 7:00 AM

// =============================================================
// TELEMETRY DATA STRUCTURE
// =============================================================

struct TelemetryData {
  uint32_t timestamp;
  long ir;
  long red;
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  int gsr;
};

#endif
