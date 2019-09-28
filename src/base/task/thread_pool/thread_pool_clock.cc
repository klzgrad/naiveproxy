// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/thread_pool_clock.h"

#include "base/logging.h"
#include "base/time/tick_clock.h"

namespace base {
namespace internal {

namespace {
const TickClock* g_tick_clock = nullptr;
}

ThreadPoolClock::ThreadPoolClock(const TickClock* tick_clock) {
  DCHECK(!g_tick_clock);
  g_tick_clock = tick_clock;
}

ThreadPoolClock::~ThreadPoolClock() {
  DCHECK(g_tick_clock);
  g_tick_clock = nullptr;
}

// static
TimeTicks ThreadPoolClock::Now() {
  // Allow |g_tick_clock| to be null so simple thread_pool/ unit tests don't
  // need to install one.
  return g_tick_clock ? g_tick_clock->NowTicks() : TimeTicks::Now();
}

}  // namespace internal
}  // namespace base
