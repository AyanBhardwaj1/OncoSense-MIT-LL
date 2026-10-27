// Host-side tests for the v3 AI pipeline: Features.cpp must match
// ml/features.py, NeuralNet.cpp must match the numpy model in ml/train.py,
// and Novelty / personalization must behave sensibly.
// Run with `make -C tests`.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../firmware/OncoSense/Features.h"
#include "../firmware/OncoSense/NeuralNet.h"
#include "../firmware/OncoSense/Novelty.h"
#include "nn_test_vectors.h"

using namespace oncosense;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabsf((float)(a) - (float)(b)) <= (tol))

static void unpack(const float *flat, MultiScan &s) {
  memcpy(s.white, flat, sizeof(s.white));
  memcpy(s.red, flat + 10, sizeof(s.red));
  memcpy(s.nir, flat + 20, sizeof(s.nir));
}

static void testFeaturesMatchPython() {
  for (int v = 0; v < kNumFeatureVectors; v++) {
    MultiScan scan, base;
    unpack(kFeatScans + v * 30, scan);
    unpack(kFeatBases + v * 30, base);
    float f[kFeatureCount];
    extractFeatures(scan, base, f);
    for (int i = 0; i < kFeatureCount; i++) CHECK_NEAR(f[i], kFeatExpected[v * 12 + i], 1e-4f);
  }
}

static void testFeaturesAreZeroAgainstSelf() {
  MultiScan s;
  for (int b = 0; b < kScanBands; b++) s.white[b] = s.red[b] = s.nir[b] = 0.1f * (b + 1);
  float f[kFeatureCount];
  extractFeatures(s, s, f);
  for (int i = 0; i < 10; i++) CHECK_NEAR(f[i], 0.0f, 1e-6f);
  // Zero / negative readings are floored instead of producing NaN.
  s.white[0] = 0.0f;
  s.white[1] = -1.0f;
  extractFeatures(s, s, f);
  CHECK(!isnan(f[0]) && !isnan(f[1]));
}

static void testInferenceMatchesNumpy() {
  NeuralNet net;
  for (int v = 0; v < kNumVectors; v++) {
    float probs[NeuralNet::kClasses];
    int best = net.predict(kVectorInputs + v * 12, probs);
    float sum = 0.0f;
    int expectedBest = 0;
    for (int c = 0; c < NeuralNet::kClasses; c++) {
      CHECK_NEAR(probs[c], kVectorProbs[v * NeuralNet::kClasses + c], 1e-4f);
      sum += probs[c];
      if (kVectorProbs[v * NeuralNet::kClasses + c] >
          kVectorProbs[v * NeuralNet::kClasses + expectedBest]) {
        expectedBest = c;
      }
    }
    CHECK_NEAR(sum, 1.0f, 1e-5f);
    CHECK(best == expectedBest);
  }
}

static void testOutOfDistribution() {
  NeuralNet net;
  float typical[12];
  memcpy(typical, kVectorInputs, sizeof(typical));
  float weird[12];
  for (int i = 0; i < 12; i++) weird[i] = 25.0f;
  CHECK(net.outOfDistribution(typical) < 3.0f);
  CHECK(net.outOfDistribution(weird) > 3.0f);
}

static void testPersonalizationLearnsAndResets() {
  NeuralNet net;
  const float *x = kVectorInputs;
  float probs[NeuralNet::kClasses];
  int before = net.predict(x, probs);
  int target = (before + 2) % NeuralNet::kClasses;  // deliberately a different class
  float fit = 0.0f;
  for (int i = 0; i < 3; i++) fit = net.personalize(x, target);
  CHECK(net.isPersonalized());
  CHECK(net.exampleCount() == 3);
  CHECK_NEAR(fit, 1.0f, 1e-6f);
  CHECK(net.predict(x, probs) == target);
  CHECK(isnan(net.personalize(x, 99)));  // invalid label rejected
  net.resetHead();
  CHECK(!net.isPersonalized());
  CHECK(net.predict(x, probs) == before);
}

static void testNovelty() {
  Novelty nov(0.05f, 15);
  float x[12];
  unsigned seed = 1;
  for (int n = 0; n < 40; n++) {
    for (int i = 0; i < 12; i++) {
      seed = seed * 1103515245u + 12345u;
      x[i] = 0.02f * (((seed >> 16) & 0x7fff) / 32767.0f - 0.5f);  // small jitter
    }
    float s = nov.scoreAndUpdate(x);
    if (n < 15) CHECK(isnan(s));  // warming up
  }
  CHECK(nov.ready());
  for (int i = 0; i < 12; i++) x[i] = 0.0f;
  CHECK(nov.score(x) < 3.0f);  // typical reading
  for (int i = 0; i < 12; i++) x[i] = 0.5f;
  CHECK(nov.score(x) > 3.0f);  // big jump in every feature
  nov.reset();
  CHECK(!nov.ready());
}

int main() {
  testFeaturesMatchPython();
  testFeaturesAreZeroAgainstSelf();
  testInferenceMatchesNumpy();
  testOutOfDistribution();
  testPersonalizationLearnsAndResets();
  testNovelty();
  printf("%d/%d AI checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
