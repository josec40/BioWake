#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "config.h"
#include "telemetry.h"

// --- Global Objects ---
TelemetryManager telemetry;
File logFile;

// --- Timing ---
uint32_t lastSampleTime = 0;
uint32_t sampleCount = 0;
bool sdReady = false;
bool sensorsReady = false;

// --- Write Buffer ---
// Buffer multiple lines before writing to SD to reduce I/O overhead
#define WRITE_BUFFER_SIZE 4096
char writeBuffer[WRITE_BUFFER_SIZE];
int bufferPos = 0;

void flushBuffer() {
    if (bufferPos > 0 && logFile) {
        logFile.write((uint8_t*)writeBuffer, bufferPos);
        logFile.flush();
        bufferPos = 0;
    }
}

void appendToBuffer(const char* line) {
    int len = strlen(line);
    // If this line would overflow the buffer, flush first
    if (bufferPos + len + 1 >= WRITE_BUFFER_SIZE) {
        flushBuffer();
    }
    memcpy(writeBuffer + bufferPos, line, len);
    bufferPos += len;
    writeBuffer[bufferPos++] = '\n';
}

bool initSD() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS)) {
        Serial.println("[FAIL] SD Card Mount Failed!");
        return false;
    }
    
    // Print card info
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[OK]   SD Card: %lluMB\n", cardSize);
    return true;
}

bool openLogFile() {
    // Generate unique filename based on boot time
    char filename[32];
    snprintf(filename, sizeof(filename), "/somnus_%lu.csv", millis());
    
    logFile = SD.open(filename, FILE_WRITE);
    if (!logFile) {
        Serial.printf("[FAIL] Could not create %s\n", filename);
        return false;
    }

    // Write CSV header
    logFile.println(telemetry.getCSVHeader());
    logFile.flush();
    Serial.printf("[OK]   Logging to: %s\n", filename);
    return true;
}

void setup() {
    Serial.begin(115200);
    
    // Wait for USB-CDC serial connection (up to 5 seconds)
    unsigned long waitStart = millis();
    while (!Serial && (millis() - waitStart < 5000)) {
        delay(100);
    }
    delay(500); // Extra settle time

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Project Somnus — SD Logging Mode");
    Serial.println("  Firmware v0.1 (Test Build)");
    Serial.println("========================================");
    Serial.println();

    // 1. Initialize Sensors
    Serial.println("[....] Initializing sensors...");
    delay(500); // Give I2C peripherals time to power up
    sensorsReady = telemetry.begin();
    if (sensorsReady) {
        Serial.println("[OK]   All sensors online.");
    } else {
        Serial.println("[WARN] One or more sensors failed. Logging will continue with available data.");
    }

    // 2. Initialize SD Card
    Serial.println("[....] Mounting SD card...");
    sdReady = initSD();
    if (!sdReady) {
        while (true) {
            Serial.println("[FATAL] SD CARD FAILED — Check wiring: CS=10, MOSI=11, SCK=12, MISO=13");
            delay(3000);
        }
    }

    // 3. Open Log File
    if (!openLogFile()) {
        while (true) {
            Serial.println("[FATAL] Cannot create log file. Check SD card is FAT32.");
            delay(3000);
        }
    }

    Serial.println();
    Serial.printf("Sampling at %dHz. Logging started.\n", SAMPLING_RATE_HZ);
    Serial.println("Pull power to stop. Data is flushed every ~500 samples (5s).");
    Serial.println("========================================");
    Serial.println();

    lastSampleTime = micros();
}

void loop() {
    uint32_t now = micros();

    // Precise 100Hz sampling (10,000 µs interval)
    if (now - lastSampleTime >= SAMPLING_PERIOD_US) {
        lastSampleTime += SAMPLING_PERIOD_US; // Additive for drift correction

        // Read all sensors
        TelemetryData data = telemetry.readSensors();

        // Format into CSV line using stack buffer (no heap allocation)
        char csvLine[128];
        telemetry.toCSV(data, csvLine, sizeof(csvLine));

        // Append to write buffer
        appendToBuffer(csvLine);

        sampleCount++;

        // Flush buffer to SD every 500 samples (5 seconds at 100Hz)
        if (sampleCount % 500 == 0) {
            flushBuffer();
        }

        // Periodic status report every 10 seconds
        if (sampleCount % 1000 == 0) {
            uint32_t runtimeSec = millis() / 1000;
            Serial.printf("[%02lu:%02lu:%02lu] Samples: %lu | Heap: %u bytes | Min Heap: %u bytes\n",
                          runtimeSec / 3600, (runtimeSec % 3600) / 60, runtimeSec % 60,
                          sampleCount, ESP.getFreeHeap(), ESP.getMinFreeHeap());
        }
    }
}
