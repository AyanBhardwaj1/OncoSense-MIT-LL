#include "Features.h"

#include <math.h>

namespace oncosense {

const char *const kFeatureNames[kFeatureCount] = {
    "d_f415", "d_f445", "d_f480", "d_f515", "d_f555", "d_f590", "d_f630", "d_f680",
    "d_red680", "d_nir910", "redness_shape", "pigment_slope",
};

static const float kFloor = 1e-6f;
static const int F3 = 2, F5 = 4, F7 = 6, F8 = 7, NIR = 9;

static float safeLog(float num, float den) {
  return logf((num > kFloor ? num : kFloor) / (den > kFloor ? den : kFloor));
}

void extractFeatures(const MultiScan &s, const MultiScan &b, float *f) {
  for (int i = 0; i < 8; i++) f[i] = safeLog(s.white[i], b.white[i]);
  f[8] = safeLog(s.red[F8], b.red[F8]);
  f[9] = safeLog(s.nir[NIR], b.nir[NIR]);
  f[10] = safeLog(s.white[F7], s.white[F5]);
  f[11] = safeLog(s.white[F8], s.white[F3]);
}

}  // namespace oncosense
