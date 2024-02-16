// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TIME_H_
#define QUICHE_QUIC_CORE_QUIC_TIME_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>

#include "absl/time/time.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QuicClock;
class QuicTime;

// A 64-bit signed integer type that stores a time duration as
// a number of microseconds. QUIC does not use absl::Duration, since the Abseil
// type is 128-bit, which would adversely affect certain performance-sensitive
// QUIC data structures.
class QUICHE_EXPORT QuicTimeDelta {
 public:
  // Creates a QuicTimeDelta from an absl::Duration. Note that this inherently
  // loses precision, since absl::Duration is nanoseconds, and QuicTimeDelta is
  // microseconds.
  explicit QuicTimeDelta(absl::Duration duration)
      : time_offset_((duration == absl::InfiniteDuration())
                         ? kInfiniteTimeUs
                         : absl::ToInt64Microseconds(duration)) {}

  // Create a object with an offset of 0.
  static constexpr QuicTimeDelta Zero() { return QuicTimeDelta(0); }

  // Create a object with infinite offset time.
  static constexpr QuicTimeDelta Infinite() {
    return QuicTimeDelta(kInfiniteTimeUs);
  }

  // Converts a number of seconds to a time offset.
  static constexpr QuicTimeDelta FromSeconds(int64_t secs) {
    return QuicTimeDelta(secs * 1000 * 1000);
  }

  // Converts a number of milliseconds to a time offset.
  static constexpr QuicTimeDelta FromMilliseconds(int64_t ms) {
    return QuicTimeDelta(ms * 1000);
  }

  // Converts a number of microseconds to a time offset.
  static constexpr QuicTimeDelta FromMicroseconds(int64_t us) {
    return QuicTimeDelta(us);
  }

  // Converts the time offset to a rounded number of seconds.
  constexpr int64_t ToSeconds() const { return time_offset_ / 1000 / 1000; }

  // Converts the time offset to a rounded number of milliseconds.
  constexpr int64_t ToMilliseconds() const { return time_offset_ / 1000; }

  // Converts the time offset to a rounded number of microseconds.
  constexpr int64_t ToMicroseconds() const { return time_offset_; }

  // Converts the time offset to an Abseil duration.
  constexpr absl::Duration ToAbsl() {
    if (ABSL_PREDICT_FALSE(IsInfinite())) {
      return absl::InfiniteDuration();
    }
    return absl::Microseconds(time_offset_);
  }

  constexpr bool IsZero() const { return time_offset_ == 0; }

  constexpr bool IsInfinite() const { return time_offset_ == kInfiniteTimeUs; }

  std::string ToDebuggingValue() const;

 private:
  friend inline bool operator==(QuicTimeDelta lhs, QuicTimeDelta rhs);
  friend inline bool operator<(QuicTimeDelta lhs, QuicTimeDelta rhs);
  friend inline QuicTimeDelta operator<<(QuicTimeDelta lhs, size_t rhs);
  friend inline QuicTimeDelta operator>>(QuicTimeDelta lhs, size_t rhs);

  friend inline constexpr QuicTimeDelta operator+(QuicTimeDelta lhs,
                                                  QuicTimeDelta rhs);
  friend inline constexpr QuicTimeDelta operator-(QuicTimeDelta lhs,
                                                  QuicTimeDelta rhs);
  friend inline constexpr QuicTimeDelta operator*(QuicTimeDelta lhs, int rhs);
  // Not constexpr since std::llround() is not constexpr.
  friend inline QuicTimeDelta operator*(QuicTimeDelta lhs, double rhs);

  friend inline QuicTime operator+(QuicTime lhs, QuicTimeDelta rhs);
  friend inline QuicTime operator-(QuicTime lhs, QuicTimeDelta rhs);
  friend inline QuicTimeDelta operator-(QuicTime lhs, QuicTime rhs);

  static constexpr int64_t kInfiniteTimeUs =
      std::numeric_limits<int64_t>::max();

  explicit constexpr QuicTimeDelta(int64_t time_offset)
      : time_offset_(time_offset) {}

  int64_t time_offset_;
  friend class QuicTime;
};

// A microsecond precision timestamp returned by a QuicClock. It is
// usually either a Unix timestamp or a timestamp returned by the
// platform-specific monotonic clock. QuicClock has a method to convert QuicTime
// to the wall time.
class QUICHE_EXPORT QuicTime {
 public:
  using Delta = QuicTimeDelta;

  // Creates a new QuicTime with an internal value of 0.  IsInitialized()
  // will return false for these times.
  static constexpr QuicTime Zero() { return QuicTime(0); }

  // Creates a new QuicTime with an infinite time.
  static constexpr QuicTime Infinite() {
    return QuicTime(Delta::kInfiniteTimeUs);
  }

  QuicTime(const QuicTime& other) = default;

  QuicTime& operator=(const QuicTime& other) {
    time_ = other.time_;
    return *this;
  }

  // Produce the internal value to be used when logging.  This value
  // represents the number of microseconds since some epoch.  It may
  // be the UNIX epoch on some platforms.  On others, it may
  // be a CPU ticks based value.
  int64_t ToDebuggingValue() const { return time_; }

  bool IsInitialized() const { return 0 != time_; }

 private:
  friend class QuicClock;

  friend inline bool operator==(QuicTime lhs, QuicTime rhs);
  friend inline bool operator<(QuicTime lhs, QuicTime rhs);
  friend inline QuicTime operator+(QuicTime lhs, QuicTimeDelta rhs);
  friend inline QuicTime operator-(QuicTime lhs, QuicTimeDelta rhs);
  friend inline QuicTimeDelta operator-(QuicTime lhs, QuicTime rhs);

  explicit constexpr QuicTime(int64_t time) : time_(time) {}

  int64_t time_;
};

// A UNIX timestamp.
//
// TODO(vasilvv): evaluate whether this can be replaced with absl::Time.
class QUICHE_EXPORT QuicWallTime {
 public:
  // FromUNIXSeconds constructs a QuicWallTime from a count of the seconds
  // since the UNIX epoch.
  static constexpr QuicWallTime FromUNIXSeconds(uint64_t seconds) {
    return QuicWallTime(seconds * 1000000);
  }

  static constexpr QuicWallTime FromUNIXMicroseconds(uint64_t microseconds) {
    return QuicWallTime(microseconds);
  }

  // Zero returns a QuicWallTime set to zero. IsZero will return true for this
  // value.
  static constexpr QuicWallTime Zero() { return QuicWallTime(0); }

  // Returns the number of seconds since the UNIX epoch.
  uint64_t ToUNIXSeconds() const;
  // Returns the number of microseconds since the UNIX epoch.
  uint64_t ToUNIXMicroseconds() const;

  bool IsAfter(QuicWallTime other) const;
  bool IsBefore(QuicWallTime other) const;

  // IsZero returns true if this object is the result of calling |Zero|.
  bool IsZero() const;

  // AbsoluteDifference returns the absolute value of the time difference
  // between |this| and |other|.
  QuicTimeDelta AbsoluteDifference(QuicWallTime other) const;

  // Add returns a new QuicWallTime that represents the time of |this| plus
  // |delta|.
  [[nodiscard]] QuicWallTime Add(QuicTimeDelta delta) const;

  // Subtract returns a new QuicWallTime that represents the time of |this|
  // minus |delta|.
  [[nodiscard]] QuicWallTime Subtract(QuicTimeDelta delta) const;

  bool operator==(const QuicWallTime& other) const {
    return microseconds_ == other.microseconds_;
  }

  QuicTimeDelta operator-(const QuicWallTime& rhs) const {
    return QuicTimeDelta::FromMicroseconds(microseconds_ - rhs.microseconds_);
  }

 private:
  explicit constexpr QuicWallTime(uint64_t microseconds)
      : microseconds_(microseconds) {}

  uint64_t microseconds_;
};

// Non-member relational operators for QuicTimeDelta.
inline bool operator==(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return lhs.time_offset_ == rhs.time_offset_;
}
inline bool operator!=(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return !(lhs == rhs);
}
inline bool operator<(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return lhs.time_offset_ < rhs.time_offset_;
}
inline bool operator>(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return rhs < lhs;
}
inline bool operator<=(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return !(rhs < lhs);
}
inline bool operator>=(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return !(lhs < rhs);
}
inline QuicTimeDelta operator<<(QuicTimeDelta lhs, size_t rhs) {
  return QuicTimeDelta(lhs.time_offset_ << rhs);
}
inline QuicTimeDelta operator>>(QuicTimeDelta lhs, size_t rhs) {
  return QuicTimeDelta(lhs.time_offset_ >> rhs);
}

// Non-member relational operators for QuicTime.
inline bool operator==(QuicTime lhs, QuicTime rhs) {
  return lhs.time_ == rhs.time_;
}
inline bool operator!=(QuicTime lhs, QuicTime rhs) { return !(lhs == rhs); }
inline bool operator<(QuicTime lhs, QuicTime rhs) {
  return lhs.time_ < rhs.time_;
}
inline bool operator>(QuicTime lhs, QuicTime rhs) { return rhs < lhs; }
inline bool operator<=(QuicTime lhs, QuicTime rhs) { return !(rhs < lhs); }
inline bool operator>=(QuicTime lhs, QuicTime rhs) { return !(lhs < rhs); }

// Override stream output operator for gtest or QUICHE_CHECK macros.
inline std::ostream& operator<<(std::ostream& output, const QuicTime t) {
  output << t.ToDebuggingValue();
  return output;
}

// Non-member arithmetic operators for QuicTimeDelta.
inline constexpr QuicTimeDelta operator+(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return QuicTimeDelta(lhs.time_offset_ + rhs.time_offset_);
}
inline constexpr QuicTimeDelta operator-(QuicTimeDelta lhs, QuicTimeDelta rhs) {
  return QuicTimeDelta(lhs.time_offset_ - rhs.time_offset_);
}
inline constexpr QuicTimeDelta operator*(QuicTimeDelta lhs, int rhs) {
  return QuicTimeDelta(lhs.time_offset_ * rhs);
}
inline QuicTimeDelta operator*(QuicTimeDelta lhs, double rhs) {
  return QuicTimeDelta(static_cast<int64_t>(
      std::llround(static_cast<double>(lhs.time_offset_) * rhs)));
}
inline QuicTimeDelta operator*(int lhs, QuicTimeDelta rhs) { return rhs * lhs; }
inline QuicTimeDelta operator*(double lhs, QuicTimeDelta rhs) {
  return rhs * lhs;
}

// Non-member arithmetic operators for QuicTime and QuicTimeDelta.
inline QuicTime operator+(QuicTime lhs, QuicTimeDelta rhs) {
  return QuicTime(lhs.time_ + rhs.time_offset_);
}
inline QuicTime operator-(QuicTime lhs, QuicTimeDelta rhs) {
  return QuicTime(lhs.time_ - rhs.time_offset_);
}
inline QuicTimeDelta operator-(QuicTime lhs, QuicTime rhs) {
  return QuicTimeDelta(lhs.time_ - rhs.time_);
}

// Override stream output operator for gtest.
inline std::ostream& operator<<(std::ostream& output,
                                const QuicTimeDelta delta) {
  output << delta.ToDebuggingValue();
  return output;
}
}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TIME_H_
