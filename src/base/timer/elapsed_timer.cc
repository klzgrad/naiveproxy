// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/timer/elapsed_timer.h"

namespace base {

ElapsedTimer::ElapsedTimer() : begin_(TimeTicks::Now()) {}

ElapsedTimer::ElapsedTimer(ElapsedTimer&& other) : begin_(other.begin_) {}

void ElapsedTimer::operator=(ElapsedTimer&& other) {
  begin_ = other.begin_;
}

TimeDelta ElapsedTimer::Elapsed() const {
  return TimeTicks::Now() - begin_;
}

ElapsedThreadTimer::ElapsedThreadTimer()
    : is_supported_(ThreadTicks::IsSupported()),
      begin_(is_supported_ ? ThreadTicks::Now() : ThreadTicks()) {}

TimeDelta ElapsedThreadTimer::Elapsed() const {
  return is_supported_ ? (ThreadTicks::Now() - begin_) : TimeDelta();
}

}  // namespace base
