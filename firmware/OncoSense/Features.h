// =============================================================================
//  OncoSense Band — feature extraction for the neural network
//
//  Mirrors ml/features.py exactly (checked by tests/test_ai.cpp). Inputs are
//  10-band arrays in band order F1..F8, Clear, NIR, one per light source,
//  plus the wearer's baseline scan for each source.
// =============================================================================
#pragma once

namespace oncosense {

static const int kFeatureCount = 12;
static const int kScanBands = 10;

// Column names used in CSV output and by ml/train.py --real.
extern const char *const kFeatureNames[kFeatureCount];

struct MultiScan {
  float white[kScanBands];   // on-board white LED
  float red[kScanBands];     // 660 nm LED
  float nir[kScanBands];     // 850 nm LED
};

// features[12]:
//   0-7  ln(white F1..F8 / baseline)      change in the visible spectrum
//   8    ln(red-LED F8 / baseline)        change at 680 nm
//   9    ln(NIR-LED NIR / baseline)       change in the near infrared
//   10   ln(white F7 / white F5)          redness shape (absolute)
//   11   ln(white F8 / white F3)          pigment slope (absolute)
void extractFeatures(const MultiScan &scan, const MultiScan &baseline, float *features);

}  // namespace oncosense
