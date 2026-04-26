#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "MAX30105.h"
#include "config.h"
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

class TelemetryManager {
public:
  TelemetryManager();
  bool begin();
  TelemetryData readSensors();
  const char *getCSVHeader();
  void toCSV(const TelemetryData &data, char *buf, size_t bufSize);

private:
  MAX30105 ppg;
  Adafruit_MPU6050 imu;
  bool ppgReady;
  bool imuReady;
};

#endif
