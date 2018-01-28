// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/time/default_tick_clock.h"

#include "base/lazy_instance.h"

namespace base {

DefaultTickClock::~DefaultTickClock() = default;

TimeTicks DefaultTickClock::NowTicks() {
  return TimeTicks::Now();
}

// static
DefaultTickClock* DefaultTickClock::GetInstance() {
  static LazyInstance<DefaultTickClock>::Leaky instance =
      LAZY_INSTANCE_INITIALIZER;
  return instance.Pointer();
}

}  // namespace base
