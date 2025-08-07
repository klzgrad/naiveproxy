// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides a macro ONLY for use in testing.
// DO NOT USE IN PRODUCTION CODE.  There are much better ways to wait.

// This code is very helpful in testing multi-threaded code, without depending
// on almost any primitives.  This is especially helpful if you are testing
// those primitive multi-threaded constructs.

// We provide a simple one argument spin wait (for 1 second), and a generic
// spin wait (for longer periods of time).

#ifndef BASE_TEST_SPIN_WAIT_H_
#define BASE_TEST_SPIN_WAIT_H_

#include "base/threading/platform_thread.h"
#include "base/time/time.h"

// Provide a macro that will wait no longer than 1 second for an asynchronous
// change is the value of an expression.
// A typical use would be:
//
//   SPIN_FOR_1_SECOND_OR_UNTIL_TRUE(0 == f(x));
//
// The expression will be evaluated repeatedly until it is true, or until
// the time (1 second) expires.
// Since tests generally have a 5 second watch dog timer, this spin loop is
// typically used to get the padding needed on a given test platform to assure
// that the test passes, even if load varies, and external events vary.

#define SPIN_FOR_1_SECOND_OR_UNTIL_TRUE(expression) \
  SPIN_FOR_TIMEDELTA_OR_UNTIL_TRUE(base::Seconds(1), (expression))

#define SPIN_FOR_TIMEDELTA_OR_UNTIL_TRUE(delta, expression)                    \
  do {                                                                         \
    base::TimeTicks spin_wait_start = base::TimeTicks::Now();                  \
    const base::TimeDelta kSpinWaitTimeout = delta;                            \
    while (!(expression)) {                                                    \
      if (kSpinWaitTimeout < base::TimeTicks::Now() - spin_wait_start) {       \
        EXPECT_LE((base::TimeTicks::Now() - spin_wait_start).InMilliseconds(), \
                  kSpinWaitTimeout.InMilliseconds())                           \
            << "Timed out";                                                    \
        break;                                                                 \
      }                                                                        \
      base::PlatformThread::Sleep(base::Milliseconds(50));                     \
    }                                                                          \
  } while (0)

#endif  // BASE_TEST_SPIN_WAIT_H_
