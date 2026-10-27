#include "Novelty.h"

#include <math.h>

namespace oncosense {

static const float kMinVar = 1e-4f;  // ~1 % change in log space

float Novelty::score(const float *x) const {
  if (!ready()) return NAN;
  float sum = 0.0f;
  for (int i = 0; i < kDims; i++) {
    float d = x[i] - mean_[i];
    float v = var_[i] > kMinVar ? var_[i] : kMinVar;
    sum += d * d / v;
  }
  return sqrtf(sum / kDims);
}

void Novelty::update(const float *x) {
  if (count_ == 0) {
    for (int i = 0; i < kDims; i++) {
      mean_[i] = x[i];
      var_[i] = 0.0f;
    }
  } else {
    // Faster learning during warm-up, then a slow drift-tracking average.
    float a = count_ < warmup_ ? 1.0f / (count_ + 1) : alpha_;
    for (int i = 0; i < kDims; i++) {
      float d = x[i] - mean_[i];
      mean_[i] += a * d;
      var_[i] = (1.0f - a) * (var_[i] + a * d * d);
    }
  }
  count_++;
}

float Novelty::scoreAndUpdate(const float *x) {
  float s = score(x);
  update(x);
  return s;
}

}  // namespace oncosense
