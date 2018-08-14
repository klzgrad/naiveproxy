// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/timer/elapsed_timer.h"

namespace base {

ElapsedTimer::ElapsedTimer() {
  begin_ = TicksNow();
}

ElapsedTimer::ElapsedTimer(ElapsedTimer&& other) {
  begin_ = other.begin_;
}

void ElapsedTimer::operator=(ElapsedTimer&& other) {
  begin_ = other.begin_;
}

TickDelta ElapsedTimer::Elapsed() const {
  return TicksDelta(TicksNow(), begin_);
}

}  // namespace base
