// =============================================================================
//  OncoSense Band — optical front end
//
//  Wraps the AS7341 spectral sensor and the LED emitters, and turns raw sensor
//  counts into ambient-corrected, gain-normalized signals:
//
//    for each of N pairs:  dark = read(LED off)   lit = read(LED on)
//    signal[ch]            = trimmedMean(lit - dark) / (gain * t_int)
//
//  Subtracting the LED-off frame rejects room light that leaks under the band;
//  dividing by gain and integration time ("basic counts") keeps readings
//  comparable when auto-gain changes the sensor range.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Adafruit_AS7341.h>

#include "config.h"

namespace oncosense {

enum class LightSource : uint8_t { Nir850, Red660, OnboardWhite };

const char *toString(LightSource s);

// AS7341 spectral channels reported to the user (skips the duplicate
// CLEAR_0 / NIR_0 readings from the first SMUX pass).
static const uint8_t kNumBands = 10;
extern const as7341_color_channel_t kBands[kNumBands];
extern const char *const kBandNames[kNumBands];

struct Frame {
  float signal[12];   // ambient-corrected basic counts, indexed by AS7341 channel
  float ambient[12];  // LED-off basic counts
  uint16_t peakRaw;   // highest raw LED-on count seen (saturation check)
  uint16_t minDeltaRaw;  // smallest LED-on minus LED-off raw delta (contact check)
  float gainX;        // sensor gain used, e.g. 64.0
  bool saturated;
  bool ok;
};

class Optics {
 public:
  bool begin();

  // Take one full differential measurement with the given emitter.
  // `watch` is the channel used for the contact check.
  bool measure(LightSource src, as7341_color_channel_t watch, Frame &out);

  float integrationMs() const { return tintMs_; }
  float gainX() const;
  uint16_t fullScale() const { return fullScale_; }

  // Turn every emitter off (also used on errors).
  void allLightsOff();

 private:
  bool readRaw(uint16_t *buf);
  void setLight(LightSource src, bool on);
  bool stepGain(int direction);
  void applyGain(as7341_gain_t g);

  Adafruit_AS7341 as7341_;
  // Each light source keeps its own auto-gain state, so cycling sources in
  // AI mode doesn't make the gain hunt back and forth.
  as7341_gain_t sourceGain_[3] = {AS_GAIN_START, AS_GAIN_START, AS_GAIN_START};
  as7341_gain_t gain_ = AS_GAIN_START;      // gain currently set on the sensor
  uint8_t activeSource_ = 0;
  float tintMs_ = 0.0f;
  uint16_t fullScale_ = 65535;
};

}  // namespace oncosense
