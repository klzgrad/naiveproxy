// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TIME_ACCUMULATOR_H_
#define QUICHE_QUIC_CORE_QUIC_TIME_ACCUMULATOR_H_

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

// QuicTimeAccumulator accumulates elapsed times between Start(s) and Stop(s).
class QUIC_EXPORT_PRIVATE QuicTimeAccumulator {
  // TODO(wub): Switch to a data member called kNotRunningSentinel after c++17.
  static constexpr QuicTime NotRunningSentinel() {
    return QuicTime::Infinite();
  }

 public:
  // True if Started and not Stopped.
  bool IsRunning() const { return last_start_time_ != NotRunningSentinel(); }

  void Start(QuicTime now) {
    DCHECK(!IsRunning());
    last_start_time_ = now;
    DCHECK(IsRunning());
  }

  void Stop(QuicTime now) {
    DCHECK(IsRunning());
    if (now > last_start_time_) {
      total_elapsed_ = total_elapsed_ + (now - last_start_time_);
    }
    last_start_time_ = NotRunningSentinel();
    DCHECK(!IsRunning());
  }

  // Get total elapsed time between COMPLETED Start/Stop pairs.
  QuicTime::Delta GetTotalElapsedTime() const { return total_elapsed_; }

  // Get total elapsed time between COMPLETED Start/Stop pairs, plus, if it is
  // running, the elapsed time between |last_start_time_| and |now|.
  QuicTime::Delta GetTotalElapsedTime(QuicTime now) const {
    if (!IsRunning()) {
      return total_elapsed_;
    }
    if (now <= last_start_time_) {
      return total_elapsed_;
    }
    return total_elapsed_ + (now - last_start_time_);
  }

 private:
  //
  //                                       |last_start_time_|
  //                                         |
  //                                         V
  // Start => Stop  =>  Start => Stop  =>  Start
  // |           |      |           |
  // |___________|  +   |___________|  =   |total_elapsed_|
  QuicTime::Delta total_elapsed_ = QuicTime::Delta::Zero();
  QuicTime last_start_time_ = NotRunningSentinel();
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TIME_ACCUMULATOR_H_
