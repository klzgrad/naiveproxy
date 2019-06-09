// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/time/time.h"

#include <zircon/syscalls.h>

#include "base/compiler_specific.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/numerics/checked_math.h"
#include "base/time/time_override.h"

namespace base {

namespace {

// Helper function to map an unsigned integer with nanosecond timebase to a
// signed integer with microsecond timebase.
ALWAYS_INLINE int64_t ZxTimeToMicroseconds(zx_time_t nanos) {
  const zx_time_t micros =
      nanos / static_cast<zx_time_t>(base::Time::kNanosecondsPerMicrosecond);
  return static_cast<int64_t>(micros);
}

}  // namespace

// Time -----------------------------------------------------------------------

namespace subtle {
Time TimeNowIgnoringOverride() {
  zx_time_t nanos_since_unix_epoch;
  zx_status_t status = zx_clock_get_new(ZX_CLOCK_UTC, &nanos_since_unix_epoch);
  ZX_CHECK(status == ZX_OK, status);
  DCHECK(nanos_since_unix_epoch != 0);
  // The following expression will overflow in the year 289938 A.D.:
  return Time() + TimeDelta::FromMicroseconds(
                      ZxTimeToMicroseconds(nanos_since_unix_epoch) +
                      Time::kTimeTToMicrosecondsOffset);
}

Time TimeNowFromSystemTimeIgnoringOverride() {
  // Just use TimeNowIgnoringOverride() because it returns the system time.
  return TimeNowIgnoringOverride();
}
}  // namespace subtle

// TimeTicks ------------------------------------------------------------------

namespace subtle {
TimeTicks TimeTicksNowIgnoringOverride() {
  const zx_time_t nanos_since_boot = zx_clock_get_monotonic();
  CHECK(nanos_since_boot != 0);
  return TimeTicks() +
         TimeDelta::FromMicroseconds(ZxTimeToMicroseconds(nanos_since_boot));
}
}  // namespace subtle

// static
TimeTicks::Clock TimeTicks::GetClock() {
  return Clock::FUCHSIA_ZX_CLOCK_MONOTONIC;
}

// static
bool TimeTicks::IsHighResolution() {
  return true;
}

// static
bool TimeTicks::IsConsistentAcrossProcesses() {
  return true;
}

// static
TimeTicks TimeTicks::FromZxTime(zx_time_t nanos_since_boot) {
  return TimeTicks(ZxTimeToMicroseconds(nanos_since_boot));
}

zx_time_t TimeTicks::ToZxTime() const {
  CheckedNumeric<zx_time_t> result(base::Time::kNanosecondsPerMicrosecond);
  result *= us_;
  return result.ValueOrDie();
}

// ThreadTicks ----------------------------------------------------------------

namespace subtle {
ThreadTicks ThreadTicksNowIgnoringOverride() {
  zx_time_t nanos_since_thread_started;
  zx_status_t status =
      zx_clock_get_new(ZX_CLOCK_THREAD, &nanos_since_thread_started);
  ZX_CHECK(status == ZX_OK, status);
  DCHECK(nanos_since_thread_started != 0);
  return ThreadTicks() + TimeDelta::FromMicroseconds(
                             ZxTimeToMicroseconds(nanos_since_thread_started));
}
}  // namespace subtle

}  // namespace base
