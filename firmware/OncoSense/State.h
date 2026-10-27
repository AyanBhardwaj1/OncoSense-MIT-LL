// =============================================================================
//  OncoSense Band — calibration state (kept in RAM and saved to flash)
// =============================================================================
#pragma once

#include <math.h>

#include "Features.h"
#include "NeuralNet.h"
#include "Optics.h"

namespace oncosense {

struct Calibration {
  // Hydration (two-point), tied to the LED it was captured with.
  float dryRef = NAN;
  float hydratedRef = NAN;
  LightSource hydrationLed = LightSource::Red660;
  // Surface scan.
  float nirBaseline = NAN;
  // Spectral: white reference card, per AS7341 channel.
  float white[12];
  bool hasWhite = false;
  // AI mode: the wearer's own multi-source baseline scan.
  MultiScan aiBaseline;
  bool hasAiBaseline = false;
};

// Everything that survives a power cycle.
struct Persisted {
  uint32_t magic;
  uint16_t version;
  Calibration cal;
  bool headValid;
  uint32_t modelId;  // which network the personalized head was trained on
  float headW[NeuralNet::kClasses * NeuralNet::kHidden];
  float headB[NeuralNet::kClasses];
};

}  // namespace oncosense
