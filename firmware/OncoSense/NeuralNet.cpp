#include "NeuralNet.h"

#include <math.h>
#include <string.h>

#include "model_weights.h"

namespace oncosense {

static_assert(model::kInputs == NeuralNet::kInputs, "model input size mismatch");
static_assert(model::kHidden2 == NeuralNet::kHidden, "model hidden size mismatch");
static_assert(model::kClasses == NeuralNet::kClasses, "model class count mismatch");

static const float kHeadL2 = 0.02f;  // pull toward factory weights

static void dense(const float *W, const float *b, const float *in, int nIn, int nOut,
                  float *out, bool relu) {
  for (int o = 0; o < nOut; o++) {
    float z = b[o];
    const float *row = W + o * nIn;
    for (int i = 0; i < nIn; i++) z += row[i] * in[i];
    out[o] = (relu && z < 0.0f) ? 0.0f : z;
  }
}

static void softmax(float *z, int n) {
  float mx = z[0];
  for (int i = 1; i < n; i++) if (z[i] > mx) mx = z[i];
  float sum = 0.0f;
  for (int i = 0; i < n; i++) {
    z[i] = expf(z[i] - mx);
    sum += z[i];
  }
  for (int i = 0; i < n; i++) z[i] /= sum;
}

NeuralNet::NeuralNet() { resetHead(); }

void NeuralNet::resetHead() {
  memcpy(headW, model::kW3, sizeof(headW));
  memcpy(headB, model::kB3, sizeof(headB));
  numExamples_ = 0;
  nextSlot_ = 0;
  personalized_ = false;
}

uint32_t NeuralNet::modelId() { return ONCOSENSE_MODEL_ID; }

const char *NeuralNet::className(int i) {
  return (i >= 0 && i < kClasses) ? model::kClassNames[i] : "unknown";
}

void NeuralNet::hiddenLayer(const float *features, float *h2) const {
  float x[kInputs], h1[model::kHidden1];
  for (int i = 0; i < kInputs; i++) x[i] = (features[i] - model::kMean[i]) / model::kStd[i];
  dense(model::kW1, model::kB1, x, kInputs, model::kHidden1, h1, true);
  dense(model::kW2, model::kB2, h1, model::kHidden1, kHidden, h2, true);
}

int NeuralNet::predict(const float *features, float *probs, float *hidden) const {
  float h2[kHidden];
  hiddenLayer(features, h2);
  dense(headW, headB, h2, kHidden, kClasses, probs, false);
  softmax(probs, kClasses);
  if (hidden) memcpy(hidden, h2, sizeof(h2));
  int best = 0;
  for (int i = 1; i < kClasses; i++) if (probs[i] > probs[best]) best = i;
  return best;
}

float NeuralNet::outOfDistribution(const float *features) const {
  float sum = 0.0f;
  for (int i = 0; i < kInputs; i++) {
    float z = (features[i] - model::kMean[i]) / model::kStd[i];
    sum += z * z;
  }
  return sqrtf(sum / kInputs);
}

float NeuralNet::personalize(const float *features, int label, int epochs, float lr) {
  if (label < 0 || label >= kClasses) return NAN;
  hiddenLayer(features, exHidden_[nextSlot_]);
  exLabel_[nextSlot_] = (uint8_t)label;
  nextSlot_ = (nextSlot_ + 1) % kMaxExamples;
  if (numExamples_ < kMaxExamples) numExamples_++;

  // Plain SGD with cross-entropy on the output layer only.
  float p[kClasses];
  for (int e = 0; e < epochs; e++) {
    for (int n = 0; n < numExamples_; n++) {
      const float *h = exHidden_[n];
      dense(headW, headB, h, kHidden, kClasses, p, false);
      softmax(p, kClasses);
      p[exLabel_[n]] -= 1.0f;  // dLoss/dLogit
      for (int o = 0; o < kClasses; o++) {
        for (int i = 0; i < kHidden; i++) {
          float w = headW[o * kHidden + i];
          headW[o * kHidden + i] -= lr * (p[o] * h[i] + kHeadL2 * (w - model::kW3[o * kHidden + i]));
        }
        headB[o] -= lr * p[o];
      }
    }
  }
  personalized_ = true;

  int correct = 0;
  for (int n = 0; n < numExamples_; n++) {
    dense(headW, headB, exHidden_[n], kHidden, kClasses, p, false);
    int best = 0;
    for (int i = 1; i < kClasses; i++) if (p[i] > p[best]) best = i;
    if (best == exLabel_[n]) correct++;
  }
  return (float)correct / numExamples_;
}

}  // namespace oncosense
