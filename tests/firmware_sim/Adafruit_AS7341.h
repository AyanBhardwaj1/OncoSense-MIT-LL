// Simulated Adafruit AS7341. Each light source returns a configurable
// per-channel signal (in "basic counts" = counts / (gain x ms)), plus ambient
// light; raw counts are clipped at the sensor's full scale like the real chip.
#pragma once

#include <stdint.h>

#include "Wire.h"

#define AS7341_I2CADDR_DEFAULT 0x39

typedef enum {
  AS7341_GAIN_0_5X, AS7341_GAIN_1X, AS7341_GAIN_2X, AS7341_GAIN_4X, AS7341_GAIN_8X,
  AS7341_GAIN_16X, AS7341_GAIN_32X, AS7341_GAIN_64X, AS7341_GAIN_128X, AS7341_GAIN_256X,
  AS7341_GAIN_512X,
} as7341_gain_t;

typedef enum {
  AS7341_CHANNEL_415nm_F1, AS7341_CHANNEL_445nm_F2, AS7341_CHANNEL_480nm_F3,
  AS7341_CHANNEL_515nm_F4, AS7341_CHANNEL_CLEAR_0, AS7341_CHANNEL_NIR_0,
  AS7341_CHANNEL_555nm_F5, AS7341_CHANNEL_590nm_F6, AS7341_CHANNEL_630nm_F7,
  AS7341_CHANNEL_680nm_F8, AS7341_CHANNEL_CLEAR, AS7341_CHANNEL_NIR,
} as7341_color_channel_t;

namespace sim {
struct Scene {
  bool present = true;       // sensor answers on I2C
  bool readsFail = false;    // readAllChannels() returns false
  bool contact = true;       // optical head on skin
  float source[3][12] = {};  // per light source (0 NIR, 1 red, 2 white), per channel
  float ambient[12] = {};
  float noise = 0.0f;        // relative multiplicative noise
};
extern Scene scene;
extern int gainSets;  // how many times setGain was called
}  // namespace sim

class Adafruit_AS7341 {
 public:
  bool begin(uint8_t addr = AS7341_I2CADDR_DEFAULT, TwoWire *wire = &Wire, int32_t id = 0);
  bool setATIME(uint8_t v) { atime_ = v; return true; }
  bool setASTEP(uint16_t v) { astep_ = v; return true; }
  bool setGain(as7341_gain_t g) { gain_ = g; sim::gainSets++; return true; }
  as7341_gain_t getGain() { return gain_; }
  bool setLEDCurrent(uint16_t ma) { ledMa_ = ma; return true; }
  bool enableLED(bool on) { onboardLed_ = on; return true; }
  bool readAllChannels(uint16_t *buf);
  bool readAllChannels() { uint16_t b[12]; return readAllChannels(b); }

 private:
  uint8_t atime_ = 0;
  uint16_t astep_ = 999;
  as7341_gain_t gain_ = AS7341_GAIN_128X;
  uint16_t ledMa_ = 0;
  bool onboardLed_ = false;
};
