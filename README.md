# BioWake — Edge AI Physiological Alarm System

BioWake is a high-performance ESP32-S3 firmware designed for real-time physiological data logging and autonomous sleep-cycle alarm triggering using Edge AI. 

## Overview
The system uses a 100Hz dual-core RTOS architecture to decouple high-frequency sensor sampling from Edge Impulse ML inference. By detecting specific sleep stages (REM/Awake) locally on the device, BioWake can autonomously trigger a wake-up sequence on a paired iPhone via Bluetooth Low Energy (BLE).

## Hardware Specs (N16R8)
- **Controller**: ESP32-S3-WROOM-1
- **Flash**: 16MB QIO 80MHz
- **PSRAM**: 8MB OPI (Used for 112KB asynchronous circular buffer)
- **Sensors**: 
  - **MAX30102**: PPG (Red/IR) @ 400Hz (Averaged to 100Hz)
  - **MPU6050**: 6-Axis IMU (Accel/Gyro) @ 100Hz
  - **GSR**: 12-bit Galvanic Skin Resistance

## Software Architecture
- **Core 1**: Dedicated to strict 100Hz sensor polling and 12.5Hz AI downsampling.
- **Core 0**: Handles SD Card I/O, BLE Keyboard HID service, and Edge Impulse Inference (TFLite Micro).
- **Asynchronous Logging**: A 4000-sample PSRAM buffer prevents data loss during SD card write latency spikes.
- **Closed Loop**: Syncs with NTP atomic time on boot, then transitions to BLE HID mode to act as an iPhone media controller.

## Trigger Logic
1. **Clock Sync**: Connects to WiFi at boot to sync UTC-4 time, then disables WiFi to prevent BLE interference.
2. **Window Check**: Monitors if the current time is within the user-defined `WAKE_WINDOW` (e.g., 6:30 AM - 7:00 AM).
3. **AI Verification**: If the Edge Impulse model predicts `REM` or `Awake_Light` with **>70% confidence**, the ESP32 sends a `KEY_MEDIA_PLAY_PAUSE` command to the iPhone.
4. **Action**: Apple Music/Spotify resumes instantly to wake the user during their lightest sleep stage.

## Setup & Deployment
1. **PlatformIO**: Open project in VS Code with PlatformIO extension.
2. **Pairing**: Boot the device and pair your iPhone to "BioWake" in Bluetooth Settings.
3. **Monitor**: Use `pio device monitor --baud 115200` to view the real-time HUD.

---
