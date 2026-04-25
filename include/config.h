#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Pin Mapping ---
// I2C Pins
#define I2C_SDA 8
#define I2C_SCL 9

// GSR Sensor (Analog)
#define GSR_PIN 4  // ADC1_CH3 on ESP32-S3 is GPIO 4

// SD Card (SPI)
#define SD_CS 10
#define SD_MOSI 11
#define SD_MISO 13
#define SD_SCK 12

// Interrupt Pins
#define MAX_INT 14
#define MPU_INT 15

// --- Timing Constants ---
#define SAMPLING_RATE_HZ 100
#define SAMPLING_PERIOD_US (1000000 / SAMPLING_RATE_HZ)
#define INFERENCE_INTERVAL_MS 5000

// --- State Machine ---
enum SystemState {
    STATE_WIFI_DEBUG,
    STATE_SD_LOGGING,
    STATE_AI_INFERENCE,
    STATE_ACTUATE
};

// --- Telemetry Structure ---
struct TelemetryData {
    uint32_t timestamp;
    long ir;
    long red;
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int gsr;
};

#endif
