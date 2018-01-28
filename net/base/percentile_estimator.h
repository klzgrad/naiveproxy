// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_PERCENTILE_ESTIMATOR_H_
#define NET_BASE_PERCENTILE_ESTIMATOR_H_

#include "base/callback.h"
#include "base/macros.h"
#include "net/base/net_export.h"

namespace net {

// This class estimates statistical percentiles (e.g. 10%l, 50%l) for
// integer distributions presented in stream form.  These estimates
// adjust automatically when the stream distribution changes.
// TODO(rdsmith): Expand the class to maintain floating point
// estimates rather than integer estimates, when there's a use case
// for that that deserves the extra complexity and pitfalls of
// floating point arithmetic.
class NET_EXPORT PercentileEstimator {
 public:
  using RandomNumberCallback = base::Callback<int(void)>;

  static const int kMedianPercentile = 50;

  // |percentile| is a number between 0 and 100 indicating what percentile
  // should be estimated (e.g. 50 would be a median estimate).
  // |initial_estimate| is the value the class is seeded with; in other
  // words, if AddSample() is never called,
  // |CurrentEstimate() == initial_estimate|.
  PercentileEstimator(int percentile, int initial_estimate);

  ~PercentileEstimator();

  int current_estimate() const { return current_estimate_; }
  void AddSample(int sample);

  // Specify a callback that will generate a "random" number
  // in the range [0,99] on each call.  Used so that tests can
  // rely on reproducible behavior.
  void SetRandomNumberGeneratorForTesting(
      RandomNumberCallback generator_callback);

 private:
  const int percentile_;

  bool sign_positive_;
  int current_estimate_;
  int current_step_;

  RandomNumberCallback generator_callback_;

  DISALLOW_COPY_AND_ASSIGN(PercentileEstimator);
};

}  // namespace net

#endif  // NET_BASE_PERCENTILE_ESTIMATOR_H_
