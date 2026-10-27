#include "SkinModel.h"

#include <math.h>

namespace oncosense {

// ------------------------------------------------------------- Statistics --

float trimmedMean(const float *values, int n) {
  if (n <= 0) return NAN;
  float sum = 0.0f, lo = values[0], hi = values[0];
  for (int i = 0; i < n; i++) {
    sum += values[i];
    if (values[i] < lo) lo = values[i];
    if (values[i] > hi) hi = values[i];
  }
  if (n < 4) return sum / n;
  return (sum - lo - hi) / (n - 2);
}

float Ema::update(float x) {
  if (isnan(x)) return value();
  if (!primed_) {
    value_ = x;
    primed_ = true;
  } else {
    value_ += alpha_ * (x - value_);
  }
  return value_;
}

// ------------------------------------------------------- Hydration model --

// Score bands. 0 = dry reference, 100 = moisturized reference.
static const float kVeryDryBelow = 25.0f;
static const float kDryBelow = 50.0f;
static const float kNormalBelow = 85.0f;
static const float kOcclusiveAbove = 130.0f;

// References closer than this (relative) cannot be told apart reliably.
static const float kMinReferenceSeparation = 0.03f;

// v1 MainSkin.ino thresholds, in v1 raw counts on the 680 nm channel.
static const float kLegacyVeryDry = 150.0f;
static const float kLegacyDry = 350.0f;
static const float kLegacyHealthy = 700.0f;

// v1 integration: (100 + 1) * (999 + 1) * 2.78 us, gain 256x.
static const float kLegacyTintMs = 101.0f * 1000.0f * 0.00278f;
static const float kLegacyGain = 256.0f;
static const float kLegacyClip = 65535.0f;

const char *toString(HydrationState s) {
  switch (s) {
    case HydrationState::VeryDry:      return "Very Dry";
    case HydrationState::Dry:          return "Dry";
    case HydrationState::Normal:       return "Normal";
    case HydrationState::WellHydrated: return "Well Hydrated";
    case HydrationState::Occlusive:    return "Occlusive Film";
    default:                           return "Unknown";
  }
}

bool referencesSeparable(float dryRef, float hydratedRef) {
  if (isnan(dryRef) || isnan(hydratedRef)) return false;
  float scale = fabsf(dryRef) > fabsf(hydratedRef) ? fabsf(dryRef) : fabsf(hydratedRef);
  return scale > 0.0f && fabsf(hydratedRef - dryRef) >= kMinReferenceSeparation * scale;
}

float hydrationScore(float signal, float dryRef, float hydratedRef) {
  if (isnan(signal) || !referencesSeparable(dryRef, hydratedRef)) return NAN;
  return 100.0f * (signal - dryRef) / (hydratedRef - dryRef);
}

HydrationState classifyScore(float score) {
  if (isnan(score)) return HydrationState::Unknown;
  if (score < kVeryDryBelow) return HydrationState::VeryDry;
  if (score < kDryBelow) return HydrationState::Dry;
  if (score < kNormalBelow) return HydrationState::Normal;
  if (score <= kOcclusiveAbove) return HydrationState::WellHydrated;
  return HydrationState::Occlusive;
}

HydrationState classifyLegacy(float legacyCounts) {
  if (isnan(legacyCounts)) return HydrationState::Unknown;
  if (legacyCounts < kLegacyVeryDry) return HydrationState::VeryDry;
  if (legacyCounts < kLegacyDry) return HydrationState::Dry;
  if (legacyCounts < kLegacyHealthy) return HydrationState::Normal;
  return HydrationState::Occlusive;  // v1 label: "Vaseline-like"
}

float toLegacyCounts(float basicCounts) {
  float counts = basicCounts * kLegacyGain * kLegacyTintMs;
  return counts > kLegacyClip ? kLegacyClip : counts;
}

// --------------------------------------------------- Surface-scan model --

const char *toString(SurfaceState s) {
  switch (s) {
    case SurfaceState::Normal:          return "Normal skin";
    case SurfaceState::OcclusiveFilm:   return "Occlusive film (oil / petrolatum)";
    case SurfaceState::MildDeviation:   return "Reduced NIR return (rash/acne-like)";
    case SurfaceState::StrongDeviation: return "Strongly reduced NIR return (scar/lesion-like)";
    default:                            return "Unknown";
  }
}

SurfaceState classifySurface(float signal, float baseline, const SurfaceThresholds &t) {
  if (isnan(signal) || !(baseline > 0.0f)) return SurfaceState::Unknown;
  float ratio = signal / baseline;
  if (ratio >= t.occlusive) return SurfaceState::OcclusiveFilm;
  if (ratio <= t.strong) return SurfaceState::StrongDeviation;
  if (ratio <= t.mild) return SurfaceState::MildDeviation;
  return SurfaceState::Normal;
}

// ------------------------------------------------- Pigment / redness model --

float melaninIndex(float reflectanceRed) {
  if (!(reflectanceRed > 0.0f)) return NAN;
  return 100.0f * log10f(1.0f / reflectanceRed);
}

float erythemaIndex(float reflectanceRed, float reflectanceGreen) {
  if (!(reflectanceRed > 0.0f) || !(reflectanceGreen > 0.0f)) return NAN;
  return 100.0f * log10f(reflectanceRed / reflectanceGreen);
}

}  // namespace oncosense
