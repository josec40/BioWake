#ifndef BLE_SLEEP_SERVICE_H
#define BLE_SLEEP_SERVICE_H

/**
 * ================================================================
 *  BioWake — BLE Sleep Stage GATT Service  (Feature 2)
 * ================================================================
 *
 *  Exposes a custom BLE GATT Characteristic that broadcasts the
 *  current AI-classified sleep stage as a single uint8_t integer.
 *
 *  Smart-home hubs (Home Assistant, Apple HomeKit Bridge, etc.)
 *  can subscribe to notifications and trigger automations:
 *    - Deep_Sleep → lower thermostat, lock doors, dim lights
 *    - REM         → (combined with GSR spike → Feature 1)
 *    - Awake_Light → turn on hallway light, start coffee maker
 *
 *  This service piggybacks on the same NimBLE server that the
 *  BLE Keyboard uses for the wake-alarm media key.
 *
 *  REQUIRES:  -D USE_NIMBLE  in build_flags
 *             h2zero/NimBLE-Arduino  in lib_deps
 * ================================================================
 */

#include <Arduino.h>
#include <NimBLEDevice.h>

// ── Sleep Stage Integer Codes ────────────────────────────────────
// These are the values written to the BLE characteristic.
// Smart-home automations should match on these exact codes.
#define SLEEP_STAGE_AWAKE_LIGHT   0
#define SLEEP_STAGE_DEEP_SLEEP    1
#define SLEEP_STAGE_REM           2
#define SLEEP_STAGE_UNKNOWN       255

// ── Public API ───────────────────────────────────────────────────

/**
 * Register the custom BLE Sleep Stage GATT service on the
 * existing NimBLE server.  MUST be called AFTER bleKeyboard.begin()
 * so the NimBLE server already exists.
 */
void initSleepStageService();

/**
 * Update the BLE characteristic with the current sleep stage.
 * The readable value is ALWAYS updated (for poll-based clients).
 * A BLE **notification** is pushed ONLY on transition INTO Deep_Sleep.
 *
 * @param newStage  One of SLEEP_STAGE_AWAKE_LIGHT / DEEP_SLEEP / REM
 */
void updateSleepStage(uint8_t newStage);

/**
 * Convert a classifier label string to its integer stage code.
 *   "Awake_Light" → 0,   "Deep_Sleep" → 1,   "REM" → 2
 */
uint8_t labelToStageCode(const String& label);

#endif // BLE_SLEEP_SERVICE_H
