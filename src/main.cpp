#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include "config.h"
#include "telemetry.h"

// =============================================================
// GLOBAL OBJECTS
// =============================================================

TelemetryManager telemetry;
File logFile;
WebServer server(BRIDGE_PORT);

// --- AI Classifier Flag (Phase 2: set by Edge Impulse model) ---
volatile bool rem_transition_detected = false;

// =============================================================
// TIMING & STATE
// =============================================================

uint32_t lastSampleTime = 0;
uint32_t sampleCount = 0;
bool sdReady = false;
bool sensorsReady = false;
bool wifiConnected = false;

// =============================================================
// PSRAM CIRCULAR QUEUE (Core 1 Writer -> Core 0 Reader)
// =============================================================

#define QUEUE_CAPACITY 4000 // ~112KB statically mapped in PSRAM
TelemetryData* dataQueue = nullptr;

volatile uint32_t queueHead = 0; // Written solely by Core 1 (100Hz Loop)
volatile uint32_t queueTail = 0; // Read solely by Core 0 (SD Writer Task)

TaskHandle_t sdWriterTaskHandle;

// =============================================================
// FREE-RTOS SD WRITER TASK (CORE 0)
// =============================================================

void sdWriterTask(void *pvParameters) {
    char csvLine[128];
    uint32_t flushCounter = 0;
    
    while (true) {
        // If there's new data waiting and SD is mounted
        if (sdReady && queueTail != queueHead) {
            // 1. Fetch struct from PSRAM
            TelemetryData data = dataQueue[queueTail];
            
            // 2. Format on Core 0 (keeping CPU cost away from Core 1)
            telemetry.toCSV(data, csvLine, sizeof(csvLine));
            
            // 3. Write directly to log file
            logFile.println(csvLine);
            
            // 4. Advance tail
            queueTail = (queueTail + 1) % QUEUE_CAPACITY;
            flushCounter++;
            
            // 5. Force a physical SD flush every 500 samples (5 seconds)
            if (flushCounter % 500 == 0) {
                logFile.flush();
            }
        } else {
            // Task yields nicely allowing WiFi to operate on Core 0 natively
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// =============================================================
// BOILERPLATE MEMORY CHECKER
// =============================================================

void checkMemory() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t freePSRAM = ESP.getFreePsram();
    uint32_t minHeap = ESP.getMinFreeHeap();
    
    uint32_t runtimeSec = millis() / 1000;
    Serial.printf("[%02lu:%02lu:%02lu] Samples: %lu | Queue: %u/%u | Free Heap: %u B | Min Heap: %u B | Free PSRAM: %u B | WiFi: %s\n",
                  runtimeSec / 3600, (runtimeSec % 3600) / 60, runtimeSec % 60,
                  sampleCount, 
                  (queueHead >= queueTail) ? (queueHead - queueTail) : (QUEUE_CAPACITY - queueTail + queueHead),
                  QUEUE_CAPACITY,
                  freeHeap, minHeap, freePSRAM,
                  wifiConnected ? "ON" : "OFF");
}

// =============================================================
// SD CARD INITIALIZATION
// =============================================================

bool initSD() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS)) {
        Serial.println("[FAIL] SD Card Mount Failed!");
        return false;
    }
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[OK]   SD Card: %lluMB\n", cardSize);
    return true;
}

bool openLogFile() {
    char filename[32];
    snprintf(filename, sizeof(filename), "/somnus_%lu.csv", millis());
    logFile = SD.open(filename, FILE_WRITE);
    if (!logFile) {
        Serial.printf("[FAIL] Could not create %s\n", filename);
        return false;
    }
    logFile.println(telemetry.getCSVHeader());
    logFile.flush();
    Serial.printf("[OK]   Logging to: %s\n", filename);
    return true;
}

// =============================================================
// WIFI STATUS BRIDGE
// =============================================================

void handleCheckAlarm() {
    server.send(200, "text/plain", rem_transition_detected ? "1" : "0");
}

void initWiFiBridge() {
    Serial.printf("[....] Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Non-blocking wait with 10-second timeout
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < WIFI_TIMEOUT_MS)) {
        delay(100); 
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("[OK]   WiFi connected: %s\n", WiFi.localIP().toString().c_str());

        if (MDNS.begin(MDNS_HOSTNAME)) {
            Serial.printf("[OK]   mDNS: http://%s.local\n", MDNS_HOSTNAME);
        } else {
            Serial.println("[WARN] mDNS failed to start.");
        }

        server.on("/check-alarm", handleCheckAlarm);
        server.begin();
        Serial.printf("[OK]   Bridge: http://%s.local/check-alarm\n", MDNS_HOSTNAME);
    } else {
        wifiConnected = false;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("[WARN] WiFi connection failed. Proceeding without bridge.");
        Serial.println("       SD logging will operate normally.");
    }
}

// =============================================================
// SETUP
// =============================================================

void setup() {
    Serial.begin(115200);

    // Wait for USB-CDC
    unsigned long waitStart = millis();
    while (!Serial && (millis() - waitStart < 5000)) {
        delay(100);
    }
    delay(500);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  Project Somnus — PSRAM / RTOS Config");
    Serial.println("========================================");
    Serial.printf("[INFO] Expected Flash: 16777216 bytes (16MB)\n");
    Serial.printf("[INFO] Actual Flash:   %u bytes\n", ESP.getFlashChipSize());
    Serial.println();

    // 0. Initialize PSRAM (Critical First Step)
    Serial.println("[....] Initializing PSRAM...");
    if (psramInit()) {
        Serial.printf("[OK]   PSRAM Online: %u bytes\n", ESP.getPsramSize());
    } else {
        Serial.println("[FATAL] PSRAM initialization failed. Check board settings.");
        while(true) delay(1000);
    }

    // Allocate Circular Buffer
    size_t allocSize = QUEUE_CAPACITY * sizeof(TelemetryData);
    dataQueue = (TelemetryData *)heap_caps_malloc(allocSize, MALLOC_CAP_SPIRAM);
    if (!dataQueue) {
        Serial.printf("[FATAL] Could not allocate %u bytes in PSRAM.\n", allocSize);
        while(true) delay(1000);
    }
    Serial.printf("[OK]   Allocated %u B in PSRAM for Circular Buffer.\n", allocSize);

    // 1. Initialize Sensors
    Serial.println("[....] Initializing sensors...");
    delay(500);
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
        Serial.println("[WARN] SD card not available. Logging disabled.");
    }

    // 3. Initialize WiFi Bridge
    initWiFiBridge();

    // 4. Open Log File
    if (sdReady && !openLogFile()) {
        sdReady = false;
        Serial.println("[WARN] Could not create log file. Logging disabled.");
    }

    // 5. Spin up SD Writer Task on Core 0
    Serial.println("[....] Spawning RTOS Writer Task on Core 0...");
    xTaskCreatePinnedToCore(
        sdWriterTask,
        "SDWriter",
        8192,
        NULL,
        1,
        &sdWriterTaskHandle,
        0
    );

    Serial.println();
    Serial.printf("Sampling at %dHz strictly tied to Core 1. Logging started.\n", SAMPLING_RATE_HZ);
    Serial.println("========================================");
    Serial.println();

    lastSampleTime = micros();
}

// =============================================================
// MAIN LOOP (CORE 1: Dedicated solely to 100Hz hardware reads)
// =============================================================

void loop() {
    uint32_t now = micros();

    // ── PRIORITY 1: Precise 100Hz Sensor Polling ──
    if (now - lastSampleTime >= SAMPLING_PERIOD_US) {
        lastSampleTime += SAMPLING_PERIOD_US; 

        // Read hardware
        TelemetryData data = telemetry.readSensors();

        // Push instantly to PSRAM ring buffer
        uint32_t nextHead = (queueHead + 1) % QUEUE_CAPACITY;
        if (nextHead != queueTail) { // Proceed only if queue isn't full
            dataQueue[queueHead] = data;
            queueHead = nextHead;
        }

        sampleCount++;

        // Status report every 10 seconds
        if (sampleCount % 1000 == 0) {
            checkMemory();
        }
    }

    // ── PRIORITY 2: Service WiFi Bridge ──
    if (wifiConnected && WiFi.status() == WL_CONNECTED) {
        server.handleClient();
    } else if (wifiConnected && WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        Serial.println("[WARN] WiFi connection lost. Bridge disabled.");
    }
}
