// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/time/time.h"

#include <cmath>
#include <ios>
#include <limits>
#include <ostream>
#include <sstream>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "base/third_party/nspr/prtime.h"
#include "build/build_config.h"

namespace base {

// TimeDelta ------------------------------------------------------------------

int TimeDelta::InDays() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(delta_ / Time::kMicrosecondsPerDay);
}

int TimeDelta::InHours() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(delta_ / Time::kMicrosecondsPerHour);
}

int TimeDelta::InMinutes() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(delta_ / Time::kMicrosecondsPerMinute);
}

double TimeDelta::InSecondsF() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(delta_) / Time::kMicrosecondsPerSecond;
}

int64_t TimeDelta::InSeconds() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_ / Time::kMicrosecondsPerSecond;
}

double TimeDelta::InMillisecondsF() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(delta_) / Time::kMicrosecondsPerMillisecond;
}

int64_t TimeDelta::InMilliseconds() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_ / Time::kMicrosecondsPerMillisecond;
}

int64_t TimeDelta::InMillisecondsRoundedUp() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return (delta_ + Time::kMicrosecondsPerMillisecond - 1) /
      Time::kMicrosecondsPerMillisecond;
}

int64_t TimeDelta::InMicroseconds() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_;
}

int64_t TimeDelta::InNanoseconds() const {
  if (is_max()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_ * Time::kNanosecondsPerMicrosecond;
}

namespace time_internal {

int64_t SaturatedAdd(TimeDelta delta, int64_t value) {
  CheckedNumeric<int64_t> rv(delta.delta_);
  rv += value;
  if (rv.IsValid())
    return rv.ValueOrDie();
  // Positive RHS overflows. Negative RHS underflows.
  if (value < 0)
    return std::numeric_limits<int64_t>::min();
  return std::numeric_limits<int64_t>::max();
}

int64_t SaturatedSub(TimeDelta delta, int64_t value) {
  CheckedNumeric<int64_t> rv(delta.delta_);
  rv -= value;
  if (rv.IsValid())
    return rv.ValueOrDie();
  // Negative RHS overflows. Positive RHS underflows.
  if (value < 0)
    return std::numeric_limits<int64_t>::max();
  return std::numeric_limits<int64_t>::min();
}

}  // namespace time_internal

std::ostream& operator<<(std::ostream& os, TimeDelta time_delta) {
  return os << time_delta.InSecondsF() << " s";
}

// Time -----------------------------------------------------------------------

// static
Time Time::FromTimeT(time_t tt) {
  if (tt == 0)
    return Time();  // Preserve 0 so we can tell it doesn't exist.
  if (tt == std::numeric_limits<time_t>::max())
    return Max();
  return Time(kTimeTToMicrosecondsOffset) + TimeDelta::FromSeconds(tt);
}

time_t Time::ToTimeT() const {
  if (is_null())
    return 0;  // Preserve 0 so we can tell it doesn't exist.
  if (is_max()) {
    // Preserve max without offset to prevent overflow.
    return std::numeric_limits<time_t>::max();
  }
  if (std::numeric_limits<int64_t>::max() - kTimeTToMicrosecondsOffset <= us_) {
    DLOG(WARNING) << "Overflow when converting base::Time with internal " <<
                     "value " << us_ << " to time_t.";
    return std::numeric_limits<time_t>::max();
  }
  return (us_ - kTimeTToMicrosecondsOffset) / kMicrosecondsPerSecond;
}

// static
Time Time::FromDoubleT(double dt) {
  if (dt == 0 || std::isnan(dt))
    return Time();  // Preserve 0 so we can tell it doesn't exist.
  return Time(kTimeTToMicrosecondsOffset) + TimeDelta::FromSecondsD(dt);
}

double Time::ToDoubleT() const {
  if (is_null())
    return 0;  // Preserve 0 so we can tell it doesn't exist.
  if (is_max()) {
    // Preserve max without offset to prevent overflow.
    return std::numeric_limits<double>::infinity();
  }
  return (static_cast<double>(us_ - kTimeTToMicrosecondsOffset) /
          static_cast<double>(kMicrosecondsPerSecond));
}

#if defined(OS_POSIX)
// static
Time Time::FromTimeSpec(const timespec& ts) {
  return FromDoubleT(ts.tv_sec +
                     static_cast<double>(ts.tv_nsec) /
                         base::Time::kNanosecondsPerSecond);
}
#endif

// static
Time Time::FromJsTime(double ms_since_epoch) {
  // The epoch is a valid time, so this constructor doesn't interpret
  // 0 as the null time.
  return Time(kTimeTToMicrosecondsOffset) +
         TimeDelta::FromMillisecondsD(ms_since_epoch);
}

double Time::ToJsTime() const {
  if (is_null()) {
    // Preserve 0 so the invalid result doesn't depend on the platform.
    return 0;
  }
  if (is_max()) {
    // Preserve max without offset to prevent overflow.
    return std::numeric_limits<double>::infinity();
  }
  return (static_cast<double>(us_ - kTimeTToMicrosecondsOffset) /
          kMicrosecondsPerMillisecond);
}

Time Time::FromJavaTime(int64_t ms_since_epoch) {
  return base::Time::UnixEpoch() +
         base::TimeDelta::FromMilliseconds(ms_since_epoch);
}

int64_t Time::ToJavaTime() const {
  if (is_null()) {
    // Preserve 0 so the invalid result doesn't depend on the platform.
    return 0;
  }
  if (is_max()) {
    // Preserve max without offset to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return ((us_ - kTimeTToMicrosecondsOffset) /
          kMicrosecondsPerMillisecond);
}

// static
Time Time::UnixEpoch() {
  Time time;
  time.us_ = kTimeTToMicrosecondsOffset;
  return time;
}

Time Time::LocalMidnight() const {
  Exploded exploded;
  LocalExplode(&exploded);
  exploded.hour = 0;
  exploded.minute = 0;
  exploded.second = 0;
  exploded.millisecond = 0;
  Time out_time;
  if (FromLocalExploded(exploded, &out_time))
    return out_time;
  // This function must not fail.
  NOTREACHED();
  return Time();
}

// static
bool Time::FromStringInternal(const char* time_string,
                              bool is_local,
                              Time* parsed_time) {
  DCHECK((time_string != nullptr) && (parsed_time != nullptr));

  if (time_string[0] == '\0')
    return false;

  PRTime result_time = 0;
  PRStatus result = PR_ParseTimeString(time_string,
                                       is_local ? PR_FALSE : PR_TRUE,
                                       &result_time);
  if (PR_SUCCESS != result)
    return false;

  result_time += kTimeTToMicrosecondsOffset;
  *parsed_time = Time(result_time);
  return true;
}

// static
bool Time::ExplodedMostlyEquals(const Exploded& lhs, const Exploded& rhs) {
  return lhs.year == rhs.year && lhs.month == rhs.month &&
         lhs.day_of_month == rhs.day_of_month && lhs.hour == rhs.hour &&
         lhs.minute == rhs.minute && lhs.second == rhs.second &&
         lhs.millisecond == rhs.millisecond;
}

std::ostream& operator<<(std::ostream& os, Time time) {
  Time::Exploded exploded;
  time.UTCExplode(&exploded);
  // Use StringPrintf because iostreams formatting is painful.
  return os << StringPrintf("%04d-%02d-%02d %02d:%02d:%02d.%03d UTC",
                            exploded.year,
                            exploded.month,
                            exploded.day_of_month,
                            exploded.hour,
                            exploded.minute,
                            exploded.second,
                            exploded.millisecond);
}

// Local helper class to hold the conversion from Time to TickTime at the
// time of the Unix epoch.
class UnixEpochSingleton {
 public:
  UnixEpochSingleton()
      : unix_epoch_(TimeTicks::Now() - (Time::Now() - Time::UnixEpoch())) {}

  TimeTicks unix_epoch() const { return unix_epoch_; }

 private:
  const TimeTicks unix_epoch_;

  DISALLOW_COPY_AND_ASSIGN(UnixEpochSingleton);
};

static LazyInstance<UnixEpochSingleton>::Leaky
    leaky_unix_epoch_singleton_instance = LAZY_INSTANCE_INITIALIZER;

// Static
TimeTicks TimeTicks::UnixEpoch() {
  return leaky_unix_epoch_singleton_instance.Get().unix_epoch();
}

TimeTicks TimeTicks::SnappedToNextTick(TimeTicks tick_phase,
                                       TimeDelta tick_interval) const {
  // |interval_offset| is the offset from |this| to the next multiple of
  // |tick_interval| after |tick_phase|, possibly negative if in the past.
  TimeDelta interval_offset = (tick_phase - *this) % tick_interval;
  // If |this| is exactly on the interval (i.e. offset==0), don't adjust.
  // Otherwise, if |tick_phase| was in the past, adjust forward to the next
  // tick after |this|.
  if (!interval_offset.is_zero() && tick_phase < *this)
    interval_offset += tick_interval;
  return *this + interval_offset;
}

std::ostream& operator<<(std::ostream& os, TimeTicks time_ticks) {
  // This function formats a TimeTicks object as "bogo-microseconds".
  // The origin and granularity of the count are platform-specific, and may very
  // from run to run. Although bogo-microseconds usually roughly correspond to
  // real microseconds, the only real guarantee is that the number never goes
  // down during a single run.
  const TimeDelta as_time_delta = time_ticks - TimeTicks();
  return os << as_time_delta.InMicroseconds() << " bogo-microseconds";
}

std::ostream& operator<<(std::ostream& os, ThreadTicks thread_ticks) {
  const TimeDelta as_time_delta = thread_ticks - ThreadTicks();
  return os << as_time_delta.InMicroseconds() << " bogo-thread-microseconds";
}

// Time::Exploded -------------------------------------------------------------

inline bool is_in_range(int value, int lo, int hi) {
  return lo <= value && value <= hi;
}

bool Time::Exploded::HasValidValues() const {
  return is_in_range(month, 1, 12) &&
         is_in_range(day_of_week, 0, 6) &&
         is_in_range(day_of_month, 1, 31) &&
         is_in_range(hour, 0, 23) &&
         is_in_range(minute, 0, 59) &&
         is_in_range(second, 0, 60) &&
         is_in_range(millisecond, 0, 999);
}

}  // namespace base
