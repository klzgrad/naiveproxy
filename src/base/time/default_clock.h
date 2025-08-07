// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TIME_DEFAULT_CLOCK_H_
#define BASE_TIME_DEFAULT_CLOCK_H_

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/time/clock.h"

namespace base {

// DefaultClock is a Clock implementation that uses Time::Now().
class BASE_EXPORT DefaultClock : public Clock {
 public:
  ~DefaultClock() override;

  // Simply returns Time::Now().
  Time Now() const override;

  // Returns a shared instance of DefaultClock. This is thread-safe.
  static DefaultClock* GetInstance();
};

}  // namespace base

#endif  // BASE_TIME_DEFAULT_CLOCK_H_
