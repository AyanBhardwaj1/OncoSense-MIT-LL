// =============================================================================
//  OncoSense Band — signal processing and skin-state models
//
//  Pure C++ with no Arduino dependencies, so it is unit-tested on a laptop
//  (see /tests). Everything here works on *ambient-corrected, normalized*
//  optical signals produced by Optics.
// =============================================================================
#pragma once

#include <stdint.h>

namespace oncosense {

// ------------------------------------------------------------- Statistics --

// Mean of `n` values after dropping the single lowest and highest value
// (falls back to a plain mean when n < 4). Robust to one-off motion spikes.
float trimmedMean(const float *values, int n);

// Exponential moving average used for the streamed "trend" value.
class Ema {
 public:
  explicit Ema(float alpha) : alpha_(alpha) {}
  float update(float x);
  void reset() { primed_ = false; }
  float value() const { return value_; }

 private:
  float alpha_;
  float value_ = 0.0f;
  bool primed_ = false;
};

// ------------------------------------------------------- Hydration model --

enum class HydrationState : uint8_t {
  Unknown,
  VeryDry,
  Dry,
  Normal,
  WellHydrated,
  Occlusive,  // beyond the hydrated reference: petrolatum / oil film
};

const char *toString(HydrationState s);

// Two-point calibrated hydration score.
//   0   -> the user's DRY reference (e.g. right after an alcohol wipe)
//   100 -> the user's MOISTURIZED reference (e.g. 10 min after moisturizer)
// Linear between the two references and *direction-agnostic*: it works whether
// moisturizing raises or lowers the return at the chosen wavelength, which
// depends on geometry and LED choice (see docs/03-optics-theory.md).
// Returns NAN when the two references are too close to separate.
float hydrationScore(float signal, float dryRef, float hydratedRef);

// Map a hydration score onto discrete states.
HydrationState classifyScore(float score);

// v1 behavior (MainSkin.ino): absolute thresholds on the 680 nm channel under
// the 660 nm LED. Used only when no two-point calibration exists yet.
// `legacyCounts` must be expressed in v1-equivalent raw counts.
HydrationState classifyLegacy(float legacyCounts);

// Convert normalized basic counts into v1-equivalent raw counts
// (v1 settings: ATIME=100, ASTEP=999, gain 256x, clipped at 65 535).
float toLegacyCounts(float basicCounts);

// --------------------------------------------------- Surface-scan model --

enum class SurfaceState : uint8_t {
  Unknown,
  Normal,
  OcclusiveFilm,  // > +20 % vs baseline
  MildDeviation,  // < -10 % vs baseline  (v1 label: rash / acne)
  StrongDeviation // < -25 % vs baseline  (v1 label: scar / dry lesion)
};

const char *toString(SurfaceState s);

struct SurfaceThresholds {
  float occlusive = 1.20f;
  float mild = 0.90f;
  float strong = 0.75f;
};

// Classify the NIR return relative to the user's own clean-skin baseline.
SurfaceState classifySurface(float signal, float baseline,
                             const SurfaceThresholds &t = SurfaceThresholds());

// ------------------------------------------------- Pigment / redness model --

// Reflectance-based indices (Dawson / Diffey style), computed from skin
// reflectance R = skin signal / white-reference signal, both 0..1.
//   Melanin index   MI = 100 * log10(1 / R_red)
//   Erythema index  EI = 100 * log10(R_red / R_green)
float melaninIndex(float reflectanceRed);
float erythemaIndex(float reflectanceRed, float reflectanceGreen);

}  // namespace oncosense
