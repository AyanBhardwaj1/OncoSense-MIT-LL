// =============================================================================
//  OncoSense Band — personal novelty detector
//
//  Learns what *this wearer's* readings normally look like (exponentially
//  weighted mean and variance per feature) and scores each new reading by its
//  RMS z-score. A sudden jump (a new product on the skin, a changed patch,
//  a band that slipped) stands out even when the classifier is confident.
// =============================================================================
#pragma once

namespace oncosense {

class Novelty {
 public:
  static const int kDims = 12;

  explicit Novelty(float alpha = 0.05f, int warmup = 15) : alpha_(alpha), warmup_(warmup) {}

  // Score a reading against the current model, then learn from it.
  // Returns NAN during warm-up.
  float scoreAndUpdate(const float *x);
  float score(const float *x) const;
  void update(const float *x);
  void reset() { count_ = 0; }
  bool ready() const { return count_ >= warmup_; }
  int count() const { return count_; }

 private:
  float alpha_;
  int warmup_;
  int count_ = 0;
  float mean_[kDims];
  float var_[kDims];
};

}  // namespace oncosense
