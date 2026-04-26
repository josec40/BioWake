#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <sntp.h>
#include <BleKeyboard.h>
#include "config.h"
#include "telemetry.h"

// =============================================================
// EDGE IMPULSE AI INFERENCE
// =============================================================
#include <biowake_v2_inferencing.h>

// =============================================================
// GLOBAL OBJECTS
// =============================================================

TelemetryManager telemetry;
File logFile;

// --- The Closed Loop Alarm ---
BleKeyboard bleKeyboard("BioWake", "Project Somnus", 100);
volatile bool alarm_fired_today = false;

// =============================================================
// TIMING & STATE
// =============================================================

uint32_t lastSampleTime = 0;
uint32_t sampleCount = 0;
bool sdReady = false;
bool sensorsReady = false;

// =============================================================
// PSRAM CIRCULAR QUEUE (SD Writer)
// =============================================================

#define QUEUE_CAPACITY 4000
TelemetryData* dataQueue = nullptr;

volatile uint32_t queueHead = 0; 
volatile uint32_t queueTail = 0; 

TaskHandle_t sdWriterTaskHandle;

// =============================================================
// AI INFERENCE STATE
// =============================================================

#define NUM_AXES         EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME  // 9
#define FEATURE_COUNT    EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE   // 558

float ai_features[FEATURE_COUNT];
float ai_inference_buffer[FEATURE_COUNT];

SemaphoreHandle_t aiMutex;
TaskHandle_t inferenceTaskHandle;

String latestPredictionLabel = "Gathering Buffer...";
float latestPredictionScore = 0.0f;
int downsampleTick = 0;

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, ai_inference_buffer + offset, length * sizeof(float));
    return 0;
}

// =============================================================
// FREE-RTOS RTOS TASKS (CORE 0)
// =============================================================

// TASK 1: SD Writer
void sdWriterTask(void *pvParameters) {
    char csvLine[128];
    uint32_t flushCounter = 0;
    
    while (true) {
        if (sdReady && queueTail != queueHead) {
            TelemetryData data = dataQueue[queueTail];
            telemetry.toCSV(data, csvLine, sizeof(csvLine));
            logFile.println(csvLine);
            
            queueTail = (queueTail + 1) % QUEUE_CAPACITY;
            flushCounter++;
            
            if (flushCounter % 500 == 0) {
                logFile.flush();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// TASK 2: Neual Network Inference
void inferenceTask(void *pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        if (xSemaphoreTake(aiMutex, portMAX_DELAY)) {
            memcpy(ai_inference_buffer, ai_features, sizeof(ai_features));
            xSemaphoreGive(aiMutex);
        }
        
        signal_t signal;
        signal.total_length = FEATURE_COUNT;
        signal.get_data = &raw_feature_get_data;
        
        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
        
        if (err == EI_IMPULSE_OK) {
            String winLabel = "";
            float winScore = -1.0;
            
            for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                if (result.classification[i].value > winScore) {
                    winScore = result.classification[i].value;
                    winLabel = result.classification[i].label;
                }
            }
            latestPredictionLabel = winLabel;
            latestPredictionScore = winScore;
            
            // ---- PHASE 3 WAKE WINDOW CLOSED LOOP ----
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                
                // Allow resetting the alarm when it's afternoon
                if (alarm_fired_today && timeinfo.tm_hour >= 12) {
                    alarm_fired_today = false; 
                }

                if (!alarm_fired_today) {
                    int currentMins = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
                    int startMins = (WAKE_WINDOW_START_HR * 60) + WAKE_WINDOW_START_MIN;
                    int endMins = startMins + WAKE_WINDOW_DUR_MINS;
                    
                    if (currentMins >= startMins && currentMins <= endMins) {
                        
                        // Strict 70% threshold check on desired wakeup labels
                        if (winLabel == "Awake_Light" || winLabel == "REM") {
                            if (winScore >= 0.70f) {
                                alarm_fired_today = true; // One-Shot trigger
                                Serial.println("\n==============================================");
                                Serial.println(" [ALARM] WAKE WINDOW MET! -> STATE: " + winLabel);
                                Serial.println("==============================================");
                                
                                if (bleKeyboard.isConnected()) {
                                    Serial.println(" [ALARM] iPhone Connected! Sending Media Button: PLAY/PAUSE...");
                                    bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
                                } else {
                                    Serial.println(" [ALARM] DANGER: iPhone is NOT CONNECTED via Bluetooth!");
                                }
                                Serial.println("==============================================\n");
                            }
                        }
                    }
                }
            }
        }
    }
}

// =============================================================
// BOILERPLATE MEMORY & AI CHECKER
// =============================================================

void checkMemory() {
    uint32_t freePSRAM = ESP.getFreePsram();
    
    // Check RTC real-world time
    struct tm timeinfo;
    char timeString[32] = "[NO NTP]";
    if (getLocalTime(&timeinfo)) {
        strftime(timeString, sizeof(timeString), "[%H:%M:%S]", &timeinfo);
    }
    
    String bleState = bleKeyboard.isConnected() ? "[iPhone Paired]" : "[BT Scanning...]";
    
    // Detailed output strictly formatted
    Serial.printf("%s %s | SD_Queue: %04u | PSRAM: %u | AI Pred: %s (%.0f%%) | Alarm: %s\n",
                  timeString, bleState.c_str(),
                  (queueHead >= queueTail) ? (queueHead - queueTail) : (QUEUE_CAPACITY - queueTail + queueHead),
                  freePSRAM,
                  latestPredictionLabel.c_str(), latestPredictionScore * 100.0f,
                  alarm_fired_today ? "FIRED" : "ARMED");
}

// =============================================================
// INITIALIZERS
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

void initNTP() {
    Serial.printf("[....] Bootstrapping WiFi for NTP Sync: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < WIFI_TIMEOUT_MS)) {
        delay(100); 
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[OK]   WiFi Connected! Syncing atomic clock...");
        configTime(UTC_OFFSET_SEC, 0, NTP_SERVER);
        
        int retry = 0;
        while (time(nullptr) < 1000000000ll && retry < 15) {
            delay(500);
            Serial.print(".");
            retry++;
        }
        Serial.println();
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.println(&timeinfo, "[OK]   Atomic Time Sync: %A, %B %d %Y %H:%M:%S");
        } else {
            Serial.println("[WARN] NTP Time Sync failed. Alarm Window compromised.");
        }
    } else {
        Serial.println("[WARN] WiFi connection failed! Alarm Window compromised.");
    }
    
    // Shut down WiFi explicitly to save battery & prevent BT antenna collisions
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[OK]   WiFi Power Disabled.");
}

// =============================================================
// SETUP
// =============================================================

void setup() {
    Serial.begin(115200);

    unsigned long waitStart = millis();
    while (!Serial && (millis() - waitStart < 5000)) delay(100);
    delay(500);

    Serial.println("\n=============================================");
    Serial.println("  BioWake — Phase 3 (The Closed Loop)");
    Serial.println("=============================================");

    aiMutex = xSemaphoreCreateMutex();
    
    // PSRAM
    Serial.println("[....] Initializing PSRAM...");
    if (psramInit()) {
        Serial.printf("[OK]   PSRAM Online: %u bytes\n", ESP.getPsramSize());
    } else {
        Serial.println("[FATAL] PSRAM init failed");
        while(true) delay(1000);
    }

    size_t allocSize = QUEUE_CAPACITY * sizeof(TelemetryData);
    dataQueue = (TelemetryData *)heap_caps_malloc(allocSize, MALLOC_CAP_SPIRAM);
    if (!dataQueue) {
        Serial.printf("[FATAL] Could not allocate PSRAM buffer\n");
        while(true) delay(1000);
    }

    // Sync NTP First before polling sensors
    initNTP();
    
    // Start BLE Keyboard Protocol
    Serial.println("[....] Starting BLE Headphone Service...");
    bleKeyboard.begin();

    Serial.println("[....] Initializing sensors...");
    delay(500);
    sensorsReady = telemetry.begin();
    if (sensorsReady) {
        Serial.println("[OK]   All sensors online.");
    } else {
        Serial.println("[WARN] Sensor init failed.");
    }

    Serial.println("[....] Mounting SD card...");
    sdReady = initSD();

    if (sdReady && !openLogFile()) {
        sdReady = false;
    }

    Serial.println("[....] Spawning RTOS Tasks on Core 0...");
    xTaskCreatePinnedToCore(sdWriterTask, "SDWriter", 8192, NULL, 1, &sdWriterTaskHandle, 0);
    xTaskCreatePinnedToCore(inferenceTask, "AI_Infer", 16384, NULL, 1, &inferenceTaskHandle, 0);

    Serial.println("\nOffline Processing Live. Connect iPhone keyboard via Bluetooth.");
    Serial.println("=============================================\n");

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

        TelemetryData data = telemetry.readSensors();

        // Push natively to SD RTOS Writer
        uint32_t nextHead = (queueHead + 1) % QUEUE_CAPACITY;
        if (nextHead != queueTail) { 
            dataQueue[queueHead] = data;
            queueHead = nextHead;
        }

        // ── PRIORITY 2: AI Downsampling (every 8th tick = 80ms) ──
        downsampleTick++;
        if (downsampleTick >= 8) {
            downsampleTick = 0;
            
            if (xSemaphoreTake(aiMutex, 0)) {
                memmove(ai_features, ai_features + NUM_AXES, (FEATURE_COUNT - NUM_AXES) * sizeof(float));
                
                int tailIdx = FEATURE_COUNT - NUM_AXES;
                ai_features[tailIdx + 0] = (float)data.ir;
                ai_features[tailIdx + 1] = (float)data.red;
                ai_features[tailIdx + 2] = (float)data.ax;
                ai_features[tailIdx + 3] = (float)data.ay;
                ai_features[tailIdx + 4] = (float)data.az;
                ai_features[tailIdx + 5] = (float)data.gx;
                ai_features[tailIdx + 6] = (float)data.gy;
                ai_features[tailIdx + 7] = (float)data.gz;
                ai_features[tailIdx + 8] = (float)data.gsr;
                
                xSemaphoreGive(aiMutex);
            }
        }

        sampleCount++;
        if (sampleCount % 1000 == 0) {
            checkMemory();
        }
    }
}
