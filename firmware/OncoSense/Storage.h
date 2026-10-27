// =============================================================================
//  OncoSense Band — persistent storage
//
//  SAMD21: emulated EEPROM in flash (FlashStorage library). Note: flash is
//          erased every time new firmware is uploaded.
//  ESP32:  NVS via Preferences.
//  Other boards: calibration lives in RAM only.
// =============================================================================
#pragma once

#include "State.h"

namespace oncosense {

bool loadState(Persisted &out);
bool saveState(const Persisted &in);
void eraseState();
const char *storageBackend();

}  // namespace oncosense
