// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A convenience class to store rtt samples and calculate smoothed rtt.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_RTT_STATS_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_RTT_STATS_H_

#include <algorithm>
#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class RttStatsPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE RttStats {
 public:
  // Calculates running standard-deviation using Welford's algorithm:
  // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#
  // Welford's_Online_algorithm.
  struct QUIC_EXPORT_PRIVATE StandardDeviationCaculator {
    StandardDeviationCaculator() {}

    // Called when a new RTT sample is available.
    void OnNewRttSample(QuicTime::Delta rtt_sample,
                        QuicTime::Delta smoothed_rtt);
    // Calculates the standard deviation.
    QuicTime::Delta CalculateStandardDeviation() const;

    bool has_valid_standard_deviation = false;

   private:
    double m2 = 0;
  };

  RttStats();
  RttStats(const RttStats&) = delete;
  RttStats& operator=(const RttStats&) = delete;

  // Updates the RTT from an incoming ack which is received |send_delta| after
  // the packet is sent and the peer reports the ack being delayed |ack_delay|.
  void UpdateRtt(QuicTime::Delta send_delta,
                 QuicTime::Delta ack_delay,
                 QuicTime now);

  // Causes the smoothed_rtt to be increased to the latest_rtt if the latest_rtt
  // is larger. The mean deviation is increased to the most recent deviation if
  // it's larger.
  void ExpireSmoothedMetrics();

  // Called when connection migrates and rtt measurement needs to be reset.
  void OnConnectionMigration();

  // Returns the EWMA smoothed RTT for the connection.
  // May return Zero if no valid updates have occurred.
  QuicTime::Delta smoothed_rtt() const { return smoothed_rtt_; }

  // Returns the EWMA smoothed RTT prior to the most recent RTT sample.
  QuicTime::Delta previous_srtt() const { return previous_srtt_; }

  QuicTime::Delta initial_rtt() const { return initial_rtt_; }

  QuicTime::Delta SmoothedOrInitialRtt() const {
    return smoothed_rtt_.IsZero() ? initial_rtt_ : smoothed_rtt_;
  }

  // Sets an initial RTT to be used for SmoothedRtt before any RTT updates.
  void set_initial_rtt(QuicTime::Delta initial_rtt) {
    if (initial_rtt.ToMicroseconds() <= 0) {
      QUIC_BUG << "Attempt to set initial rtt to <= 0.";
      return;
    }
    initial_rtt_ = initial_rtt;
  }

  // The most recent rtt measurement.
  // May return Zero if no valid updates have occurred.
  QuicTime::Delta latest_rtt() const { return latest_rtt_; }

  // Returns the min_rtt for the entire connection.
  // May return Zero if no valid updates have occurred.
  QuicTime::Delta min_rtt() const { return min_rtt_; }

  QuicTime::Delta mean_deviation() const { return mean_deviation_; }

  // Returns standard deviation if there is a valid one. Otherwise, returns
  // mean_deviation_.
  QuicTime::Delta GetStandardOrMeanDeviation() const;

  QuicTime last_update_time() const { return last_update_time_; }

  bool ignore_max_ack_delay() const { return ignore_max_ack_delay_; }

  void set_ignore_max_ack_delay(bool ignore_max_ack_delay) {
    ignore_max_ack_delay_ = ignore_max_ack_delay;
  }

  void set_initial_max_ack_delay(QuicTime::Delta initial_max_ack_delay) {
    max_ack_delay_ = std::max(max_ack_delay_, initial_max_ack_delay);
  }

  void EnableStandardDeviationCalculation() {
    calculate_standard_deviation_ = true;
  }

 private:
  friend class test::RttStatsPeer;

  QuicTime::Delta latest_rtt_;
  QuicTime::Delta min_rtt_;
  QuicTime::Delta smoothed_rtt_;
  QuicTime::Delta previous_srtt_;
  // Mean RTT deviation during this session.
  // Approximation of standard deviation, the error is roughly 1.25 times
  // larger than the standard deviation, for a normally distributed signal.
  QuicTime::Delta mean_deviation_;
  // Standard deviation calculator. Only used calculate_standard_deviation_ is
  // true.
  StandardDeviationCaculator standard_deviation_calculator_;
  bool calculate_standard_deviation_;
  QuicTime::Delta initial_rtt_;
  // The maximum ack delay observed over the connection after excluding ack
  // delays that were too large to be included in an RTT measurement.
  // TODO(ianswett): Remove when deprecating quic_sent_packet_manager_cleanup.
  QuicTime::Delta max_ack_delay_;
  QuicTime last_update_time_;
  // Whether to ignore the peer's max ack delay.
  bool ignore_max_ack_delay_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_RTT_STATS_H_
