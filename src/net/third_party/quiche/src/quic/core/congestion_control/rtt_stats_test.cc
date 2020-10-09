// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"

#include <cmath>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mock_log.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/rtt_stats_peer.h"

using testing::_;
using testing::Message;

namespace quic {
namespace test {

class RttStatsTest : public QuicTest {
 protected:
  RttStats rtt_stats_;
};

TEST_F(RttStatsTest, DefaultsBeforeUpdate) {
  EXPECT_LT(QuicTime::Delta::Zero(), rtt_stats_.initial_rtt());
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.min_rtt());
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.smoothed_rtt());
}

TEST_F(RttStatsTest, SmoothedRtt) {
  // Verify that ack_delay is ignored in the first measurement.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.smoothed_rtt());
  // Verify that a plausible ack delay increases the max ack delay.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(400),
                       QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.smoothed_rtt());
  // Verify that Smoothed RTT includes max ack delay if it's reasonable.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(350),
                       QuicTime::Delta::FromMilliseconds(50), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.smoothed_rtt());
  // Verify that large erroneous ack_delay does not change Smoothed RTT.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(200),
                       QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(287500),
            rtt_stats_.smoothed_rtt());
}

TEST_F(RttStatsTest, SmoothedRttIgnoreAckDelay) {
  rtt_stats_.set_ignore_max_ack_delay(true);
  // Verify that ack_delay is ignored in the first measurement.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.smoothed_rtt());
  // Verify that a plausible ack delay increases the max ack delay.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.smoothed_rtt());
  // Verify that Smoothed RTT includes max ack delay if it's reasonable.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Delta::FromMilliseconds(50), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(300), rtt_stats_.smoothed_rtt());
  // Verify that large erroneous ack_delay does not change Smoothed RTT.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(200),
                       QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(287500),
            rtt_stats_.smoothed_rtt());
}

// Ensure that the potential rounding artifacts in EWMA calculation do not cause
// the SRTT to drift too far from the exact value.
TEST_F(RttStatsTest, SmoothedRttStability) {
  for (size_t time = 3; time < 20000; time++) {
    RttStats stats;
    for (size_t i = 0; i < 100; i++) {
      stats.UpdateRtt(QuicTime::Delta::FromMicroseconds(time),
                      QuicTime::Delta::FromMilliseconds(0), QuicTime::Zero());
      int64_t time_delta_us = stats.smoothed_rtt().ToMicroseconds() - time;
      ASSERT_LE(std::abs(time_delta_us), 1);
    }
  }
}

TEST_F(RttStatsTest, PreviousSmoothedRtt) {
  // Verify that ack_delay is corrected for in Smoothed RTT.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(200),
                       QuicTime::Delta::FromMilliseconds(0), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.smoothed_rtt());
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.previous_srtt());
  // Ensure the previous SRTT is 200ms after a 100ms sample.
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(100), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMicroseconds(187500).ToMicroseconds(),
            rtt_stats_.smoothed_rtt().ToMicroseconds());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.previous_srtt());
}

TEST_F(RttStatsTest, MinRtt) {
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(200),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.min_rtt());
  rtt_stats_.UpdateRtt(
      QuicTime::Delta::FromMilliseconds(10), QuicTime::Delta::Zero(),
      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(10));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), rtt_stats_.min_rtt());
  rtt_stats_.UpdateRtt(
      QuicTime::Delta::FromMilliseconds(50), QuicTime::Delta::Zero(),
      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(20));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), rtt_stats_.min_rtt());
  rtt_stats_.UpdateRtt(
      QuicTime::Delta::FromMilliseconds(50), QuicTime::Delta::Zero(),
      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(30));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), rtt_stats_.min_rtt());
  rtt_stats_.UpdateRtt(
      QuicTime::Delta::FromMilliseconds(50), QuicTime::Delta::Zero(),
      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(40));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(10), rtt_stats_.min_rtt());
  // Verify that ack_delay does not go into recording of min_rtt_.
  rtt_stats_.UpdateRtt(
      QuicTime::Delta::FromMilliseconds(7),
      QuicTime::Delta::FromMilliseconds(2),
      QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(50));
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(7), rtt_stats_.min_rtt());
}

TEST_F(RttStatsTest, ExpireSmoothedMetrics) {
  QuicTime::Delta initial_rtt = QuicTime::Delta::FromMilliseconds(10);
  rtt_stats_.UpdateRtt(initial_rtt, QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(initial_rtt, rtt_stats_.min_rtt());
  EXPECT_EQ(initial_rtt, rtt_stats_.smoothed_rtt());

  EXPECT_EQ(0.5 * initial_rtt, rtt_stats_.mean_deviation());

  // Update once with a 20ms RTT.
  QuicTime::Delta doubled_rtt = 2 * initial_rtt;
  rtt_stats_.UpdateRtt(doubled_rtt, QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(1.125 * initial_rtt, rtt_stats_.smoothed_rtt());

  // Expire the smoothed metrics, increasing smoothed rtt and mean deviation.
  rtt_stats_.ExpireSmoothedMetrics();
  EXPECT_EQ(doubled_rtt, rtt_stats_.smoothed_rtt());
  EXPECT_EQ(0.875 * initial_rtt, rtt_stats_.mean_deviation());

  // Now go back down to 5ms and expire the smoothed metrics, and ensure the
  // mean deviation increases to 15ms.
  QuicTime::Delta half_rtt = 0.5 * initial_rtt;
  rtt_stats_.UpdateRtt(half_rtt, QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_GT(doubled_rtt, rtt_stats_.smoothed_rtt());
  EXPECT_LT(initial_rtt, rtt_stats_.mean_deviation());
}

TEST_F(RttStatsTest, UpdateRttWithBadSendDeltas) {
  // Make sure we ignore bad RTTs.
  CREATE_QUIC_MOCK_LOG(log);

  QuicTime::Delta initial_rtt = QuicTime::Delta::FromMilliseconds(10);
  rtt_stats_.UpdateRtt(initial_rtt, QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(initial_rtt, rtt_stats_.min_rtt());
  EXPECT_EQ(initial_rtt, rtt_stats_.smoothed_rtt());

  std::vector<QuicTime::Delta> bad_send_deltas;
  bad_send_deltas.push_back(QuicTime::Delta::Zero());
  bad_send_deltas.push_back(QuicTime::Delta::Infinite());
  bad_send_deltas.push_back(QuicTime::Delta::FromMicroseconds(-1000));
  log.StartCapturingLogs();

  for (QuicTime::Delta bad_send_delta : bad_send_deltas) {
    SCOPED_TRACE(Message() << "bad_send_delta = "
                           << bad_send_delta.ToMicroseconds());
    if (QUIC_LOG_WARNING_IS_ON()) {
      EXPECT_QUIC_LOG_CALL_CONTAINS(log, WARNING, "Ignoring");
    }
    rtt_stats_.UpdateRtt(bad_send_delta, QuicTime::Delta::Zero(),
                         QuicTime::Zero());
    EXPECT_EQ(initial_rtt, rtt_stats_.min_rtt());
    EXPECT_EQ(initial_rtt, rtt_stats_.smoothed_rtt());
  }
}

TEST_F(RttStatsTest, ResetAfterConnectionMigrations) {
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(200),
                       QuicTime::Delta::FromMilliseconds(0), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.smoothed_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.min_rtt());

  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(300),
                       QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.smoothed_rtt());
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(200), rtt_stats_.min_rtt());

  // Reset rtt stats on connection migrations.
  rtt_stats_.OnConnectionMigration();
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.latest_rtt());
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.smoothed_rtt());
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.min_rtt());
}

TEST_F(RttStatsTest, StandardDeviationCaculatorTest1) {
  // All samples are the same.
  rtt_stats_.EnableStandardDeviationCalculation();
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(rtt_stats_.mean_deviation(),
            rtt_stats_.GetStandardOrMeanDeviation());

  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_EQ(QuicTime::Delta::Zero(), rtt_stats_.GetStandardOrMeanDeviation());
}

TEST_F(RttStatsTest, StandardDeviationCaculatorTest2) {
  // Small variance.
  rtt_stats_.EnableStandardDeviationCalculation();
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(10),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(9),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(11),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_LT(QuicTime::Delta::FromMicroseconds(500),
            rtt_stats_.GetStandardOrMeanDeviation());
  EXPECT_GT(QuicTime::Delta::FromMilliseconds(1),
            rtt_stats_.GetStandardOrMeanDeviation());
}

TEST_F(RttStatsTest, StandardDeviationCaculatorTest3) {
  // Some variance.
  rtt_stats_.EnableStandardDeviationCalculation();
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(50),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(50),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_APPROX_EQ(rtt_stats_.mean_deviation(),
                   rtt_stats_.GetStandardOrMeanDeviation(), 0.25f);
}

}  // namespace test
}  // namespace quic
