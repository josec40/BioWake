#include "ble_sleep_service.h"

// =============================================================
// CUSTOM BLE UUIDS  (BioWake Sleep Stage Namespace)
// =============================================================
//
// Service:        b104ae01-0001-4000-8000-00805f9b34fb
// Characteristic: b104ae01-0002-4000-8000-00805f9b34fb
//
// "b104ae" ≈ "BIOWAKE" in hex-speak.
// -0001 = service, -0002 = characteristic.

#define SLEEP_SERVICE_UUID     "b104ae01-0001-4000-8000-00805f9b34fb"
#define SLEEP_STAGE_CHAR_UUID  "b104ae01-0002-4000-8000-00805f9b34fb"

// ── Module-level state ───────────────────────────────────────────

static NimBLECharacteristic* pSleepStageChar = nullptr;
static uint8_t previousStage = SLEEP_STAGE_UNKNOWN;

// =================================================================
//  initSleepStageService()
//
//  Piggy-backs on the NimBLE server that bleKeyboard.begin() created.
//  Adds a custom GATT service with one Read+Notify characteristic.
// =================================================================

void initSleepStageService() {
    NimBLEServer* pServer = NimBLEDevice::getServer();
    if (!pServer) {
        Serial.println("[FAIL] BLE Sleep Stage: NimBLE server not found!");
        Serial.println("       Did bleKeyboard.begin() run first?");
        return;
    }

    // Pause advertising while we register the new service
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->stop();

    // Create the custom GATT service
    NimBLEService* pService = pServer->createService(SLEEP_SERVICE_UUID);

    // Create the sleep stage characteristic: Read + Notify
    pSleepStageChar = pService->createCharacteristic(
        SLEEP_STAGE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    // Set initial value to UNKNOWN (model hasn't run yet)
    uint8_t initVal = SLEEP_STAGE_UNKNOWN;
    pSleepStageChar->setValue(&initVal, 1);

    // Start the service
    pService->start();

    // Resume advertising (BLE Keyboard config is preserved by NimBLE)
    pAdvertising->start();

    Serial.println("[OK]   BLE Sleep Stage Service registered");
    Serial.printf("       Service UUID : %s\n", SLEEP_SERVICE_UUID);
    Serial.printf("       Stage Char   : %s\n", SLEEP_STAGE_CHAR_UUID);
    Serial.println("       Codes: 0=Awake_Light  1=Deep_Sleep  2=REM  255=Unknown");
}

// =================================================================
//  updateSleepStage()
//
//  Called every inference cycle (~2s) from the AI task.
//    - ALWAYS updates the readable characteristic value.
//    - Pushes a BLE notification ONLY on transition INTO Deep_Sleep.
//      (Smart-home hub reacts to this notification as the trigger.)
// =================================================================

void updateSleepStage(uint8_t newStage) {
    if (pSleepStageChar == nullptr) return;

    // Always update the readable value (for poll-based clients)
    pSleepStageChar->setValue(&newStage, 1);

    // Push BLE notification ONLY on transition INTO Deep_Sleep
    if (newStage == SLEEP_STAGE_DEEP_SLEEP && previousStage != SLEEP_STAGE_DEEP_SLEEP) {
        pSleepStageChar->notify();
        Serial.println("[BLE-SMART-HOME] Deep_Sleep transition → Notification sent to subscribed clients");
    }

    previousStage = newStage;
}

// =================================================================
//  labelToStageCode()
//
//  Maps the Edge Impulse classifier label string to an integer code.
// =================================================================

uint8_t labelToStageCode(const String& label) {
    if (label == "Awake_Light") return SLEEP_STAGE_AWAKE_LIGHT;
    if (label == "Deep_Sleep")  return SLEEP_STAGE_DEEP_SLEEP;
    if (label == "REM")         return SLEEP_STAGE_REM;
    return SLEEP_STAGE_UNKNOWN;
}
