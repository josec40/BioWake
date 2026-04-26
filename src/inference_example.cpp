/**
 * ================================================================
 *  BioWake — Edge Impulse Inference Example  (ESP32-S3)
 * ================================================================
 *
 *  This file shows the hardware team exactly how to:
 *    1. #include the trained BioWake ML library
 *    2. Fill a feature buffer from live sensor data
 *    3. Call run_classifier() and read the results
 *
 *  Model details (from model_metadata.h / model_variables.h):
 *    - Project:    BioWake_v2  (Edge Impulse #975133)
 *    - Labels:     Awake_Light | Deep_Sleep | REM
 *    - Input:      62 samples × 9 axes = 558 raw features
 *    - Axes order: IR, Red, Accel_X, Accel_Y, Accel_Z,
 *                  Gyro_X, Gyro_Y, Gyro_Z, GSR
 *    - Frequency:  12.5 Hz  (interval_ms = 80)
 *    - Quant:      int8
 *
 *  NOTE: This file is an EXAMPLE — it is NOT compiled by default.
 *        To use it, integrate the inference logic below into your
 *        main.cpp loop (or a dedicated FreeRTOS task).
 *
 *  To exclude from build while keeping it in the repo, either:
 *    a) Rename to inference_example.cpp.example, or
 *    b) Wrap everything in:  #ifdef BUILD_INFERENCE_EXAMPLE
 * ================================================================
 */

#ifdef BUILD_INFERENCE_EXAMPLE   // <-- guard so it won't clash with main.cpp

#include <Arduino.h>

// ── 1. Include the Edge Impulse inferencing library ──────────────
//    PlatformIO will find this automatically because it lives under
//    lib/biowake_v2_inferencing/
#include <biowake_v2_inferencing.h>

// ── 2. Bring in our hardware helpers ─────────────────────────────
#include "config.h"      // TelemetryData struct, pin defs
#include "telemetry.h"   // TelemetryManager (sensor reads)

// ── Constants derived from the model ─────────────────────────────
static const int   NUM_AXES         = EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;  // 9
static const int   NUM_SAMPLES      = EI_CLASSIFIER_RAW_SAMPLE_COUNT;       // 62
static const int   FEATURE_COUNT    = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;   // 558
static const float SAMPLE_INTERVAL  = EI_CLASSIFIER_INTERVAL_MS;            // 80 ms

// ── Sensor + feature buffer ──────────────────────────────────────
TelemetryManager telemetry;
static float features[FEATURE_COUNT];

// =================================================================
//  raw_feature_get_data()  — callback used by run_classifier()
//  It copies data out of our flat `features[]` array on demand.
// =================================================================
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}

// =================================================================
//  fill_features_from_sensors()
//  Collects 62 samples at 12.5 Hz (80 ms apart) from real hardware
//  and packs them into the flat features[] buffer the model expects.
//
//  Buffer layout (row-major):
//    [ IR_0, Red_0, AX_0, AY_0, AZ_0, GX_0, GY_0, GZ_0, GSR_0,
//      IR_1, Red_1, AX_1, ...                              GSR_61 ]
// =================================================================
void fill_features_from_sensors() {
    for (int s = 0; s < NUM_SAMPLES; s++) {
        TelemetryData d = telemetry.readSensors();

        int base = s * NUM_AXES;
        features[base + 0] = (float)d.ir;
        features[base + 1] = (float)d.red;
        features[base + 2] = (float)d.ax;
        features[base + 3] = (float)d.ay;
        features[base + 4] = (float)d.az;
        features[base + 5] = (float)d.gx;
        features[base + 6] = (float)d.gy;
        features[base + 7] = (float)d.gz;
        features[base + 8] = (float)d.gsr;

        delay((int)SAMPLE_INTERVAL);  // 80 ms → 12.5 Hz
    }
}

// =================================================================
//  fill_features_with_dummy()
//  For bench-testing on the ESP32 without sensors wired up.
//  Fills the buffer with zeros so you can verify the inference
//  pipeline compiles, runs, and returns valid probabilities.
// =================================================================
void fill_features_with_dummy() {
    for (int i = 0; i < FEATURE_COUNT; i++) {
        features[i] = 0.0f;
    }
}

// =================================================================
//  run_inference()  — the main classification call
//  Returns the index of the winning label (0–2).
// =================================================================
int run_inference(bool use_dummy_data = false) {
    // 1. Fill the feature buffer
    if (use_dummy_data) {
        fill_features_with_dummy();
    } else {
        fill_features_from_sensors();
    }

    // 2. Build the signal structure Edge Impulse expects
    signal_t signal;
    signal.total_length = FEATURE_COUNT;
    signal.get_data     = &raw_feature_get_data;

    // 3. Run the classifier
    ei_impulse_result_t result = {};
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false /* debug */);

    if (err != EI_IMPULSE_OK) {
        Serial.printf("[EI] run_classifier() failed: %d\n", (int)err);
        return -1;
    }

    // 4. Print results & find the winner
    Serial.println("──── BioWake Inference Result ────");
    Serial.printf("  DSP time:       %d ms\n",  result.timing.dsp);
    Serial.printf("  Inference time: %d ms\n",  result.timing.classification);
    Serial.printf("  Anomaly score:  %.3f\n",   result.timing.anomaly);
    Serial.println();

    int   best_idx   = 0;
    float best_score = 0.0f;

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        float val = result.classification[ix].value;
        Serial.printf("  %-12s : %.4f\n",
                       result.classification[ix].label, val);
        if (val > best_score) {
            best_score = val;
            best_idx   = ix;
        }
    }

    Serial.printf("\n  → Predicted: %s (%.1f%%)\n",
                  result.classification[best_idx].label,
                  best_score * 100.0f);
    Serial.println("─────────────────────────────────");

    return best_idx;
}

// =================================================================
//  SETUP / LOOP  — minimal harness for standalone testing
// =================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000) { delay(100); }

    Serial.println();
    Serial.println("========================================");
    Serial.println("  BioWake — Edge Impulse Inference Demo");
    Serial.println("========================================");

    // Print model info
    Serial.printf("  Model:   %s\n", EI_CLASSIFIER_PROJECT_NAME);
    Serial.printf("  Labels:  %d  (%s)\n",
                  EI_CLASSIFIER_LABEL_COUNT,
                  EI_CLASSIFIER_FUSION_AXES_STRING);
    Serial.printf("  Window:  %d samples × %d axes = %d features\n",
                  NUM_SAMPLES, NUM_AXES, FEATURE_COUNT);
    Serial.printf("  Freq:    %.1f Hz  (interval %d ms)\n",
                  (float)EI_CLASSIFIER_FREQUENCY,
                  (int)EI_CLASSIFIER_INTERVAL_MS);
    Serial.println("========================================\n");

    // Initialize sensors (comment out if bench-testing without HW)
    // telemetry.begin();
}

void loop() {
    // ── Option A: Run with dummy data (no sensors needed) ────────
    int prediction = run_inference(/* use_dummy_data */ true);

    // ── Option B: Run with real sensor data ──────────────────────
    // int prediction = run_inference(/* use_dummy_data */ false);

    // ── Use the prediction ───────────────────────────────────────
    // prediction == 0  →  "Awake_Light"
    // prediction == 1  →  "Deep_Sleep"
    // prediction == 2  →  "REM"
    //
    // Example: set the alarm flag when REM is detected
    // if (prediction == 2) {
    //     rem_transition_detected = true;
    // }

    Serial.println("\n[Waiting 5 seconds before next inference...]\n");
    delay(5000);
}

#endif // BUILD_INFERENCE_EXAMPLE
