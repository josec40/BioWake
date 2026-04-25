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

// Interrupt Pins
#define MAX_INT 14
#define MPU_INT 15

// =============================================================
// SAMPLING CONFIGURATION
// =============================================================

#define SAMPLING_RATE_HZ 100
#define SAMPLING_PERIOD_US (1000000 / SAMPLING_RATE_HZ)

// =============================================================
// WIFI STATUS BRIDGE
// =============================================================

#define WIFI_SSID "ProjectSomnus"
#define WIFI_PASS "Pass1234"
#define WIFI_TIMEOUT_MS 10000  // 10-second connection timeout
#define MDNS_HOSTNAME "somnus" // Discoverable as somnus.local
#define BRIDGE_PORT 80

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
