// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TIMER_ELAPSED_TIMER_H_
#define BASE_TIMER_ELAPSED_TIMER_H_

#include "base/macros.h"
#include "util/ticks.h"

namespace base {

// A simple wrapper around TicksNow().
class ElapsedTimer {
 public:
  ElapsedTimer();
  ElapsedTimer(ElapsedTimer&& other);

  void operator=(ElapsedTimer&& other);

  // Returns the time elapsed since object construction.
  TickDelta Elapsed() const;

 private:
  Ticks begin_;

  DISALLOW_COPY_AND_ASSIGN(ElapsedTimer);
};

}  // namespace base

#endif  // BASE_TIMER_ELAPSED_TIMER_H_
