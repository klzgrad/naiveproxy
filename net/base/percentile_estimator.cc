// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "percentile_estimator.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/rand_util.h"

namespace {

// Random number wrapper to allow substitutions for testing.
int GenerateRand0To99() {
  return base::RandInt(0, 99);
}

}  // namespace

namespace net {

// The algorithm used for percentile estimation is "Algorithm 3" from
// https://arxiv.org/pdf/1407.1121v1.pdf.  There are several parts to the
// algorithm:
// * The estimate is conditionally moved towards the sample by a step amount.
//   This means that if the samples are clustered around a value the estimates
//   will converge to that sample.
// * The percentile requested (e.g. 90%l) is handled by the conditional move.
//   If the estimate is accurate, there is a chance equal to the percentile
//   value that a sample will be lower than it, and a chance equal to
//   1-percentile that it will be higher.  So the code balances those
//   probabilities by increasing the estimate in the percentile fraction
//   of the cases where the sample is over the estimate, and decreases the
//   estimate in (1-percentile) fraction of the cases where the sample is under
//   the estimate.
//   E.g. in the case of the 90%l estimation, the estimate would
//   move up in 90% of the cases in which the sample was above the
//   estimate (which would be 10% of the total samples, presuming the
//   estimate was accurate), and it would move down in 10% of the cases
//   in which the sample was below the estimate.
// * Every time the estimate moves in the same direction, the step
//   amount is increased by one, and every time the estimate reverses
//   direction, the step amount is decreased (to 1, if greater than 1,
//   by one, if zero or negative).  The effective step amount is
//   Max(step, 1).
// * If the estimate
//   would be moved beyond the sample causing its move, it is moved to
//   be equal to the same (and the step amount set to the distance to
//   the sample).  See the paper for further details.

PercentileEstimator::PercentileEstimator(int percentile, int initial_estimate)
    : percentile_(percentile),
      sign_positive_(true),
      current_estimate_(initial_estimate),
      current_step_(1),
      generator_callback_(base::Bind(&GenerateRand0To99)) {}

PercentileEstimator::~PercentileEstimator() = default;

void PercentileEstimator::AddSample(int sample) {
  int rand100 = generator_callback_.Run();
  if (sample > current_estimate_ && rand100 > 1 - percentile_) {
    current_step_ += sign_positive_ ? 1 : -1;
    current_estimate_ += (current_step_ > 0) ? current_step_ : 1;

    // Clamp movement to distance to sample.
    if (current_estimate_ > sample) {
      current_step_ -= current_estimate_ - sample;
      current_estimate_ = sample;
    }

    // If we've reversed direction, reset the step down.
    if (!sign_positive_ && current_step_ > 1)
      current_step_ = 1;

    sign_positive_ = true;
  } else if (sample < current_estimate_ && rand100 > percentile_) {
    current_step_ += !sign_positive_ ? 1 : -1;
    current_estimate_ -= (current_step_ > 0) ? current_step_ : 1;

    // Clamp movement to distance to sample.
    if (current_estimate_ < sample) {
      current_step_ -= sample - current_estimate_;
      current_estimate_ = sample;
    }

    // If we've reversed direction, reset the step down.
    if (sign_positive_ && current_step_ > 1)
      current_step_ = 1;

    sign_positive_ = false;
  }
}

void PercentileEstimator::SetRandomNumberGeneratorForTesting(
    RandomNumberCallback generator_callback) {
  generator_callback_ = generator_callback;
}

}  // namespace net
