// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/simple_test_clock.h"

namespace base {

SimpleTestClock::SimpleTestClock() = default;

SimpleTestClock::~SimpleTestClock() = default;

Time SimpleTestClock::Now() const {
  AutoLock lock(lock_);
  return now_;
}

void SimpleTestClock::Advance(TimeDelta delta) {
  AutoLock lock(lock_);
  now_ += delta;
}

void SimpleTestClock::SetNow(Time now) {
  AutoLock lock(lock_);
  now_ = now;
}

}  // namespace base
