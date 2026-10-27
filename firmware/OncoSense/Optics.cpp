#include "Optics.h"

#include "SkinModel.h"

namespace oncosense {

const as7341_color_channel_t kBands[kNumBands] = {
    AS7341_CHANNEL_415nm_F1, AS7341_CHANNEL_445nm_F2, AS7341_CHANNEL_480nm_F3,
    AS7341_CHANNEL_515nm_F4, AS7341_CHANNEL_555nm_F5, AS7341_CHANNEL_590nm_F6,
    AS7341_CHANNEL_630nm_F7, AS7341_CHANNEL_680nm_F8, AS7341_CHANNEL_CLEAR,
    AS7341_CHANNEL_NIR,
};

const char *const kBandNames[kNumBands] = {
    "f415", "f445", "f480", "f515", "f555",
    "f590", "f630", "f680", "clear", "nir",
};

static const uint8_t kMaxPairs = 8;
static const uint8_t kMaxGainRetries = 3;
static const float kSaturationFraction = 0.90f;
static const float kUnderRangeFraction = 0.10f;

static_assert(PAIRS_PER_MEASUREMENT >= 1 && PAIRS_PER_MEASUREMENT <= kMaxPairs,
              "PAIRS_PER_MEASUREMENT must be 1..8");

const char *toString(LightSource s) {
  switch (s) {
    case LightSource::Nir850:       return "850nm";
    case LightSource::Red660:       return "660nm";
    case LightSource::OnboardWhite: return "white";
  }
  return "?";
}

bool Optics::begin() {
  pinMode(PIN_LED_NIR, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_NIR, LOW);
  digitalWrite(PIN_LED_RED, LOW);

#if USE_CUSTOM_I2C_PINS
  SENSOR_WIRE.begin(CUSTOM_SDA_PIN, CUSTOM_SCL_PIN);
#endif

  if (!as7341_.begin(AS7341_I2CADDR_DEFAULT, &SENSOR_WIRE)) return false;

  as7341_.setATIME(AS_ATIME);
  as7341_.setASTEP(AS_ASTEP);
  as7341_.setGain(gain_);
  as7341_.setLEDCurrent(ONBOARD_LED_MA);
  as7341_.enableLED(false);

  uint32_t steps = (uint32_t)(AS_ATIME + 1) * (uint32_t)(AS_ASTEP + 1);
  tintMs_ = steps * 0.00278f;
  fullScale_ = steps > 65535UL ? 65535 : (uint16_t)steps;
  return true;
}

float Optics::gainX() const {
  return gain_ == AS7341_GAIN_0_5X ? 0.5f : (float)(1UL << ((uint8_t)gain_ - 1));
}

void Optics::applyGain(as7341_gain_t g) {
  if (g != gain_) {
    gain_ = g;
    as7341_.setGain(gain_);
  }
}

bool Optics::stepGain(int direction) {
  int next = (int)gain_ + direction;
  if (next < (int)AS7341_GAIN_0_5X || next > (int)AS7341_GAIN_512X) return false;
  applyGain((as7341_gain_t)next);
  sourceGain_[activeSource_] = gain_;
  return true;
}

void Optics::setLight(LightSource src, bool on) {
  switch (src) {
    case LightSource::Nir850:       digitalWrite(PIN_LED_NIR, on ? HIGH : LOW); break;
    case LightSource::Red660:       digitalWrite(PIN_LED_RED, on ? HIGH : LOW); break;
    case LightSource::OnboardWhite: as7341_.enableLED(on); break;
  }
}

void Optics::allLightsOff() {
  digitalWrite(PIN_LED_NIR, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  as7341_.enableLED(false);
}

bool Optics::readRaw(uint16_t *buf) { return as7341_.readAllChannels(buf); }

bool Optics::measure(LightSource src, as7341_color_channel_t watch, Frame &out) {
  static float delta[12][kMaxPairs];
  static float dark[12][kMaxPairs];
  uint16_t litRaw[12], darkRaw[12];

  out.ok = false;
  activeSource_ = (uint8_t)src;
  applyGain(sourceGain_[activeSource_]);
  for (uint8_t attempt = 0; attempt <= kMaxGainRetries; attempt++) {
    out.peakRaw = 0;
    out.minDeltaRaw = 65535;
    out.saturated = false;

    for (uint8_t p = 0; p < PAIRS_PER_MEASUREMENT; p++) {
      setLight(src, false);
      delay(LED_SETTLE_MS);
      bool okDark = readRaw(darkRaw);

      setLight(src, true);
      delay(LED_SETTLE_MS);
      bool okLit = readRaw(litRaw);
      setLight(src, false);

      if (!okDark || !okLit) {
        allLightsOff();
        return false;
      }

      for (uint8_t b = 0; b < kNumBands; b++) {
        uint8_t ch = kBands[b];
        delta[ch][p] = (float)litRaw[ch] - (float)darkRaw[ch];
        dark[ch][p] = (float)darkRaw[ch];
        if (litRaw[ch] > out.peakRaw) out.peakRaw = litRaw[ch];
      }
      int32_t d = (int32_t)litRaw[watch] - (int32_t)darkRaw[watch];
      uint16_t dClamped = d < 0 ? 0 : (uint16_t)d;
      if (dClamped < out.minDeltaRaw) out.minDeltaRaw = dClamped;
    }

    out.saturated = out.peakRaw >= kSaturationFraction * fullScale_;
    // Saturated data is useless: drop the gain and measure again.
    if (out.saturated && AUTO_GAIN && stepGain(-1)) continue;
    break;
  }

  out.gainX = gainX();
  float toBasic = 1.0f / (out.gainX * tintMs_);
  for (uint8_t i = 0; i < 12; i++) {
    out.signal[i] = NAN;
    out.ambient[i] = NAN;
  }
  for (uint8_t b = 0; b < kNumBands; b++) {
    uint8_t ch = kBands[b];
    out.signal[ch] = trimmedMean(delta[ch], PAIRS_PER_MEASUREMENT) * toBasic;
    out.ambient[ch] = trimmedMean(dark[ch], PAIRS_PER_MEASUREMENT) * toBasic;
  }

  // Weak signal: raise the gain for the *next* measurement.
  if (AUTO_GAIN && !out.saturated && out.peakRaw < kUnderRangeFraction * fullScale_) {
    stepGain(+1);
  }

  out.ok = true;
  return true;
}

}  // namespace oncosense
