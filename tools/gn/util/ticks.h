// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_TICKS_H_
#define UTIL_TICKS_H_

#include <stdint.h>

using Ticks = uint64_t;

class TickDelta {
 public:
  explicit TickDelta(uint64_t delta) : delta_(delta) {}

  double InSecondsF() const { return delta_ / 1000000000.0; }
  double InMillisecondsF() const { return delta_ / 1000000.0; }
  double InMicrosecondsF() const { return delta_ / 1000.0; }
  double InNanosecondsF() const { return delta_; }

  uint64_t InSeconds() const { return delta_ / 1000000000; }
  uint64_t InMilliseconds() const { return delta_ / 1000000; }
  uint64_t InMicroseconds() const { return delta_ / 1000; }
  uint64_t InNanoseconds() const { return delta_; }

  uint64_t raw() const { return delta_; }

 private:
  uint64_t delta_;
};

Ticks TicksNow();

TickDelta TicksDelta(Ticks new_ticks, Ticks old_ticks);

class ElapsedTimer {
 public:
  ElapsedTimer() : start_(TicksNow()) {}
  TickDelta Elapsed() { return TicksDelta(TicksNow(), start_); }

 private:
  Ticks start_;
};

#endif  // UTIL_TICKS_H_
