// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QuicBandwidth represents a bandwidth, stored in bits per second resolution.

#ifndef QUICHE_QUIC_CORE_QUIC_BANDWIDTH_H_
#define QUICHE_QUIC_CORE_QUIC_BANDWIDTH_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"

namespace quic {

class QUICHE_EXPORT QuicBandwidth {
 public:
  // Creates a new QuicBandwidth with an internal value of 0.
  static constexpr QuicBandwidth Zero() { return QuicBandwidth(0); }

  // Creates a new QuicBandwidth with an internal value of INT64_MAX.
  static constexpr QuicBandwidth Infinite() {
    return QuicBandwidth(std::numeric_limits<int64_t>::max());
  }

  // Create a new QuicBandwidth holding the bits per second.
  static constexpr QuicBandwidth FromBitsPerSecond(int64_t bits_per_second) {
    return QuicBandwidth(bits_per_second);
  }

  // Create a new QuicBandwidth holding the kilo bits per second.
  static constexpr QuicBandwidth FromKBitsPerSecond(int64_t k_bits_per_second) {
    return QuicBandwidth(k_bits_per_second * 1000);
  }

  // Create a new QuicBandwidth holding the bytes per second.
  static constexpr QuicBandwidth FromBytesPerSecond(int64_t bytes_per_second) {
    return QuicBandwidth(bytes_per_second * 8);
  }

  // Create a new QuicBandwidth holding the kilo bytes per second.
  static constexpr QuicBandwidth FromKBytesPerSecond(
      int64_t k_bytes_per_second) {
    return QuicBandwidth(k_bytes_per_second * 8000);
  }

  // Create a new QuicBandwidth based on the bytes per the elapsed delta.
  static QuicBandwidth FromBytesAndTimeDelta(QuicByteCount bytes,
                                             QuicTime::Delta delta) {
    if (bytes == 0) {
      return QuicBandwidth(0);
    }

    // 1 bit is 1000000 micro bits.
    int64_t num_micro_bits = 8 * bytes * kNumMicrosPerSecond;
    if (num_micro_bits < delta.ToMicroseconds()) {
      return QuicBandwidth(1);
    }

    return QuicBandwidth(num_micro_bits / delta.ToMicroseconds());
  }

  int64_t ToBitsPerSecond() const { return bits_per_second_; }

  int64_t ToKBitsPerSecond() const { return bits_per_second_ / 1000; }

  int64_t ToBytesPerSecond() const { return bits_per_second_ / 8; }

  int64_t ToKBytesPerSecond() const { return bits_per_second_ / 8000; }

  constexpr QuicByteCount ToBytesPerPeriod(QuicTime::Delta time_period) const {
    return bits_per_second_ * time_period.ToMicroseconds() / 8 /
           kNumMicrosPerSecond;
  }

  int64_t ToKBytesPerPeriod(QuicTime::Delta time_period) const {
    return bits_per_second_ * time_period.ToMicroseconds() / 8000 /
           kNumMicrosPerSecond;
  }

  bool IsZero() const { return bits_per_second_ == 0; }
  bool IsInfinite() const {
    return bits_per_second_ == Infinite().ToBitsPerSecond();
  }

  constexpr QuicTime::Delta TransferTime(QuicByteCount bytes) const {
    if (bits_per_second_ == 0) {
      return QuicTime::Delta::Zero();
    }
    return QuicTime::Delta::FromMicroseconds(bytes * 8 * kNumMicrosPerSecond /
                                             bits_per_second_);
  }

  std::string ToDebuggingValue() const;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, QuicBandwidth bandwidth) {
    sink.Append(bandwidth.ToDebuggingValue());
  }

 private:
  explicit constexpr QuicBandwidth(int64_t bits_per_second)
      : bits_per_second_(bits_per_second >= 0 ? bits_per_second : 0) {}

  int64_t bits_per_second_;

  friend constexpr QuicBandwidth operator+(QuicBandwidth lhs,
                                           QuicBandwidth rhs);
  friend constexpr QuicBandwidth operator-(QuicBandwidth lhs,
                                           QuicBandwidth rhs);
  friend QuicBandwidth operator*(QuicBandwidth lhs, float rhs);
};

// Non-member relational operators for QuicBandwidth.
inline bool operator==(QuicBandwidth lhs, QuicBandwidth rhs) {
  return lhs.ToBitsPerSecond() == rhs.ToBitsPerSecond();
}
inline bool operator!=(QuicBandwidth lhs, QuicBandwidth rhs) {
  return !(lhs == rhs);
}
inline bool operator<(QuicBandwidth lhs, QuicBandwidth rhs) {
  return lhs.ToBitsPerSecond() < rhs.ToBitsPerSecond();
}
inline bool operator>(QuicBandwidth lhs, QuicBandwidth rhs) {
  return rhs < lhs;
}
inline bool operator<=(QuicBandwidth lhs, QuicBandwidth rhs) {
  return !(rhs < lhs);
}
inline bool operator>=(QuicBandwidth lhs, QuicBandwidth rhs) {
  return !(lhs < rhs);
}

// Non-member arithmetic operators for QuicBandwidth.
inline constexpr QuicBandwidth operator+(QuicBandwidth lhs, QuicBandwidth rhs) {
  return QuicBandwidth(lhs.bits_per_second_ + rhs.bits_per_second_);
}
inline constexpr QuicBandwidth operator-(QuicBandwidth lhs, QuicBandwidth rhs) {
  return QuicBandwidth(lhs.bits_per_second_ - rhs.bits_per_second_);
}
inline QuicBandwidth operator*(QuicBandwidth lhs, float rhs) {
  return QuicBandwidth(
      static_cast<int64_t>(std::llround(lhs.bits_per_second_ * rhs)));
}
inline QuicBandwidth operator*(float lhs, QuicBandwidth rhs) {
  return rhs * lhs;
}
inline constexpr QuicByteCount operator*(QuicBandwidth lhs,
                                         QuicTime::Delta rhs) {
  return lhs.ToBytesPerPeriod(rhs);
}
inline constexpr QuicByteCount operator*(QuicTime::Delta lhs,
                                         QuicBandwidth rhs) {
  return rhs * lhs;
}

// Override stream output operator for gtest.
inline std::ostream& operator<<(std::ostream& output,
                                const QuicBandwidth bandwidth) {
  output << bandwidth.ToDebuggingValue();
  return output;
}

}  // namespace quic
#endif  // QUICHE_QUIC_CORE_QUIC_BANDWIDTH_H_
