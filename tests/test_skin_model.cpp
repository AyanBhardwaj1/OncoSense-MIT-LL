// Host-side unit tests for firmware/OncoSense/SkinModel.cpp.
// Run with `make -C tests` (no Arduino hardware needed).

#include <math.h>
#include <stdio.h>

#include "../firmware/OncoSense/SkinModel.h"

using namespace oncosense;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                 \
  } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabsf((float)(a) - (float)(b)) <= (tol))

static void testTrimmedMean() {
  float spike[] = {10, 11, 9, 10, 500};  // one motion spike
  CHECK_NEAR(trimmedMean(spike, 5), 10.333f, 0.01f);
  float three[] = {1, 2, 6};             // too few to trim: plain mean
  CHECK_NEAR(trimmedMean(three, 3), 3.0f, 1e-6f);
  CHECK(isnan(trimmedMean(three, 0)));
}

static void testEma() {
  Ema e(0.5f);
  CHECK_NEAR(e.update(10), 10, 1e-6f);   // first sample primes the filter
  CHECK_NEAR(e.update(20), 15, 1e-6f);
  CHECK_NEAR(e.update(NAN), 15, 1e-6f);  // NaN is ignored
  e.reset();
  CHECK_NEAR(e.update(4), 4, 1e-6f);
}

static void testHydrationScore() {
  // Moisturizing *raises* the signal.
  CHECK_NEAR(hydrationScore(1.0f, 1.0f, 2.0f), 0.0f, 1e-4f);
  CHECK_NEAR(hydrationScore(2.0f, 1.0f, 2.0f), 100.0f, 1e-4f);
  CHECK_NEAR(hydrationScore(1.5f, 1.0f, 2.0f), 50.0f, 1e-4f);
  // Moisturizing *lowers* the signal (reduced scattering) - same scale.
  CHECK_NEAR(hydrationScore(2.0f, 2.0f, 1.0f), 0.0f, 1e-4f);
  CHECK_NEAR(hydrationScore(1.0f, 2.0f, 1.0f), 100.0f, 1e-4f);
  CHECK_NEAR(hydrationScore(1.25f, 2.0f, 1.0f), 75.0f, 1e-4f);
  // References too close together cannot be separated.
  CHECK(isnan(hydrationScore(1.0f, 1.0f, 1.01f)));
  CHECK(isnan(hydrationScore(NAN, 1.0f, 2.0f)));
  CHECK(isnan(hydrationScore(1.0f, 0.0f, 0.0f)));
}

static void testClassifyScore() {
  CHECK(classifyScore(-10) == HydrationState::VeryDry);
  CHECK(classifyScore(10) == HydrationState::VeryDry);
  CHECK(classifyScore(30) == HydrationState::Dry);
  CHECK(classifyScore(60) == HydrationState::Normal);
  CHECK(classifyScore(100) == HydrationState::WellHydrated);
  CHECK(classifyScore(130) == HydrationState::WellHydrated);
  CHECK(classifyScore(131) == HydrationState::Occlusive);
  CHECK(classifyScore(NAN) == HydrationState::Unknown);
}

static void testLegacyCompatibility() {
  // Same boundaries as v1 MainSkin.ino.
  CHECK(classifyLegacy(149) == HydrationState::VeryDry);
  CHECK(classifyLegacy(150) == HydrationState::Dry);
  CHECK(classifyLegacy(349) == HydrationState::Dry);
  CHECK(classifyLegacy(350) == HydrationState::Normal);
  CHECK(classifyLegacy(699) == HydrationState::Normal);
  CHECK(classifyLegacy(700) == HydrationState::Occlusive);
  // 1 basic count == 256x gain * 280.78 ms of integration in v1 raw counts.
  CHECK_NEAR(toLegacyCounts(0.01f), 0.01f * 256.0f * 280.78f, 0.5f);
  CHECK_NEAR(toLegacyCounts(10.0f), 65535.0f, 1e-3f);  // v1 clipped here
}

static void testSurfaceScan() {
  // Same ratios as v1 AdaSpecCode.ino: +20 %, -10 %, -25 %.
  CHECK(classifySurface(100, 100) == SurfaceState::Normal);
  CHECK(classifySurface(120, 100) == SurfaceState::OcclusiveFilm);
  CHECK(classifySurface(119, 100) == SurfaceState::Normal);
  CHECK(classifySurface(90, 100) == SurfaceState::MildDeviation);
  CHECK(classifySurface(80, 100) == SurfaceState::MildDeviation);
  CHECK(classifySurface(75, 100) == SurfaceState::StrongDeviation);
  CHECK(classifySurface(50, 100) == SurfaceState::StrongDeviation);
  CHECK(classifySurface(100, NAN) == SurfaceState::Unknown);
  CHECK(classifySurface(100, 0) == SurfaceState::Unknown);
  SurfaceThresholds loose;
  loose.mild = 0.80f;
  CHECK(classifySurface(85, 100, loose) == SurfaceState::Normal);
}

static void testPigmentIndices() {
  CHECK_NEAR(melaninIndex(1.0f), 0.0f, 1e-4f);
  CHECK_NEAR(melaninIndex(0.1f), 100.0f, 1e-3f);
  CHECK(isnan(melaninIndex(0.0f)));
  CHECK_NEAR(erythemaIndex(0.5f, 0.5f), 0.0f, 1e-4f);
  CHECK(erythemaIndex(0.6f, 0.3f) > 0.0f);  // red > green reflectance
  CHECK(isnan(erythemaIndex(0.5f, 0.0f)));
}

static void testStrings() {
  CHECK(toString(HydrationState::Occlusive)[0] != '\0');
  CHECK(toString(SurfaceState::Unknown)[0] != '\0');
}

int main() {
  testTrimmedMean();
  testEma();
  testHydrationScore();
  testClassifyScore();
  testLegacyCompatibility();
  testSurfaceScan();
  testPigmentIndices();
  testStrings();
  printf("%d/%d checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
