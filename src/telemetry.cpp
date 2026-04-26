#include "telemetry.h"

TelemetryManager::TelemetryManager() : ppgReady(false), imuReady(false) {}

bool TelemetryManager::begin() {
  bool success = true;

  // Initialize I2C bus once (shared by MAX30102 and MPU6050)
  Wire.begin(I2C_SDA, I2C_SCL);

  // --- MAX30102 (PPG) at 0x57 ---
  if (!ppg.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("       MAX30102 (0x57): NOT FOUND");
    success = false;
  } else {
    byte ledBrightness = 60;
    byte sampleAverage = 4;
    byte ledMode = 2; // Red + IR
    int sampleRate = 400; // 400Hz / 4 (Average) = 100Hz Native Output
    int pulseWidth = 411;
    int adcRange = 4096;
    ppg.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth,
              adcRange);
    ppgReady = true;
    Serial.println("       MAX30102 (0x57): OK");
  }

  // --- MPU6050 (IMU) at 0x68 ---
  if (!imu.begin(0x68, &Wire)) {
    Serial.println("       MPU6050  (0x68): NOT FOUND");
    success = false;
  } else {
    imu.setAccelerometerRange(MPU6050_RANGE_2_G);
    imu.setGyroRange(MPU6050_RANGE_250_DEG);
    imu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    imuReady = true;
    Serial.println("       MPU6050  (0x68): OK");
  }

  // --- GSR (Analog) on GPIO 4 ---
  analogReadResolution(12); // 0-4095
  pinMode(GSR_PIN, INPUT);
  Serial.println("       GSR      (GPIO4): OK");

  return success;
}

TelemetryData TelemetryManager::readSensors() {
  TelemetryData data;
  data.timestamp = millis();

  // PPG — returns 0 if sensor not available
  if (ppgReady) {
    data.ir = ppg.getIR();
    data.red = ppg.getRed();
  } else {
    data.ir = 0;
    data.red = 0;
  }

  // IMU — returns 0 if sensor not available
  if (imuReady) {
    sensors_event_t a, g, temp;
    imu.getEvent(&a, &g, &temp);
    // Scale floats to int (m/s² and rad/s × 100 for 2 decimal places)
    data.ax = (int16_t)(a.acceleration.x * 100);
    data.ay = (int16_t)(a.acceleration.y * 100);
    data.az = (int16_t)(a.acceleration.z * 100);
    data.gx = (int16_t)(g.gyro.x * 100);
    data.gy = (int16_t)(g.gyro.y * 100);
    data.gz = (int16_t)(g.gyro.z * 100);
  } else {
    data.ax = data.ay = data.az = 0;
    data.gx = data.gy = data.gz = 0;
  }

  // GSR — Inverse logic: high sweat = low voltage, so invert
  int rawGsr = analogRead(GSR_PIN);
  data.gsr = 4095 - rawGsr;

  return data;
}

const char *TelemetryManager::getCSVHeader() {
  return "Timestamp,IR,Red,Accel_X,Accel_Y,Accel_Z,Gyro_X,Gyro_Y,Gyro_Z,GSR";
}

void TelemetryManager::toCSV(const TelemetryData &data, char *buf,
                             size_t bufSize) {
  snprintf(buf, bufSize, "%lu,%ld,%ld,%d,%d,%d,%d,%d,%d,%d", data.timestamp,
           data.ir, data.red, data.ax, data.ay, data.az, data.gx, data.gy,
           data.gz, data.gsr);
}
