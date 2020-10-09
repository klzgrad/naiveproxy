// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/cubic_bytes.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {
namespace test {
namespace {

const float kBeta = 0.7f;          // Default Cubic backoff factor.
const float kBetaLastMax = 0.85f;  // Default Cubic backoff factor.
const uint32_t kNumConnections = 2;
const float kNConnectionBeta = (kNumConnections - 1 + kBeta) / kNumConnections;
const float kNConnectionBetaLastMax =
    (kNumConnections - 1 + kBetaLastMax) / kNumConnections;
const float kNConnectionAlpha = 3 * kNumConnections * kNumConnections *
                                (1 - kNConnectionBeta) / (1 + kNConnectionBeta);

}  // namespace

class CubicBytesTest : public QuicTest {
 protected:
  CubicBytesTest()
      : one_ms_(QuicTime::Delta::FromMilliseconds(1)),
        hundred_ms_(QuicTime::Delta::FromMilliseconds(100)),
        cubic_(&clock_) {}

  QuicByteCount RenoCwndInBytes(QuicByteCount current_cwnd) {
    QuicByteCount reno_estimated_cwnd =
        current_cwnd +
        kDefaultTCPMSS * (kNConnectionAlpha * kDefaultTCPMSS) / current_cwnd;
    return reno_estimated_cwnd;
  }

  QuicByteCount ConservativeCwndInBytes(QuicByteCount current_cwnd) {
    QuicByteCount conservative_cwnd = current_cwnd + kDefaultTCPMSS / 2;
    return conservative_cwnd;
  }

  QuicByteCount CubicConvexCwndInBytes(QuicByteCount initial_cwnd,
                                       QuicTime::Delta rtt,
                                       QuicTime::Delta elapsed_time) {
    const int64_t offset =
        ((elapsed_time + rtt).ToMicroseconds() << 10) / 1000000;
    const QuicByteCount delta_congestion_window =
        ((410 * offset * offset * offset) * kDefaultTCPMSS >> 40);
    const QuicByteCount cubic_cwnd = initial_cwnd + delta_congestion_window;
    return cubic_cwnd;
  }

  QuicByteCount LastMaxCongestionWindow() {
    return cubic_.last_max_congestion_window();
  }

  QuicTime::Delta MaxCubicTimeInterval() {
    return cubic_.MaxCubicTimeInterval();
  }

  const QuicTime::Delta one_ms_;
  const QuicTime::Delta hundred_ms_;
  MockClock clock_;
  CubicBytes cubic_;
};

// TODO(jokulik): The original "AboveOrigin" test, below, is very
// loose.  It's nearly impossible to make the test tighter without
// deploying the fix for convex mode.  Once cubic convex is deployed,
// replace "AboveOrigin" with this test.
TEST_F(CubicBytesTest, AboveOriginWithTighterBounds) {
  // Convex growth.
  const QuicTime::Delta rtt_min = hundred_ms_;
  int64_t rtt_min_ms = rtt_min.ToMilliseconds();
  float rtt_min_s = rtt_min_ms / 1000.0;
  QuicByteCount current_cwnd = 10 * kDefaultTCPMSS;
  const QuicByteCount initial_cwnd = current_cwnd;

  clock_.AdvanceTime(one_ms_);
  const QuicTime initial_time = clock_.ApproximateNow();
  const QuicByteCount expected_first_cwnd = RenoCwndInBytes(current_cwnd);
  current_cwnd = cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd,
                                                 rtt_min, initial_time);
  ASSERT_EQ(expected_first_cwnd, current_cwnd);

  // Normal TCP phase.
  // The maximum number of expected Reno RTTs is calculated by
  // finding the point where the cubic curve and the reno curve meet.
  const int max_reno_rtts =
      std::sqrt(kNConnectionAlpha / (.4 * rtt_min_s * rtt_min_s * rtt_min_s)) -
      2;
  for (int i = 0; i < max_reno_rtts; ++i) {
    // Alternatively, we expect it to increase by one, every time we
    // receive current_cwnd/Alpha acks back.  (This is another way of
    // saying we expect cwnd to increase by approximately Alpha once
    // we receive current_cwnd number ofacks back).
    const uint64_t num_acks_this_epoch =
        current_cwnd / kDefaultTCPMSS / kNConnectionAlpha;
    const QuicByteCount initial_cwnd_this_epoch = current_cwnd;
    for (QuicPacketCount n = 0; n < num_acks_this_epoch; ++n) {
      // Call once per ACK.
      const QuicByteCount expected_next_cwnd = RenoCwndInBytes(current_cwnd);
      current_cwnd = cubic_.CongestionWindowAfterAck(
          kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
      ASSERT_EQ(expected_next_cwnd, current_cwnd);
    }
    // Our byte-wise Reno implementation is an estimate.  We expect
    // the cwnd to increase by approximately one MSS every
    // cwnd/kDefaultTCPMSS/Alpha acks, but it may be off by as much as
    // half a packet for smaller values of current_cwnd.
    const QuicByteCount cwnd_change_this_epoch =
        current_cwnd - initial_cwnd_this_epoch;
    ASSERT_NEAR(kDefaultTCPMSS, cwnd_change_this_epoch, kDefaultTCPMSS / 2);
    clock_.AdvanceTime(hundred_ms_);
  }

  for (int i = 0; i < 54; ++i) {
    const uint64_t max_acks_this_epoch = current_cwnd / kDefaultTCPMSS;
    const QuicTime::Delta interval = QuicTime::Delta::FromMicroseconds(
        hundred_ms_.ToMicroseconds() / max_acks_this_epoch);
    for (QuicPacketCount n = 0; n < max_acks_this_epoch; ++n) {
      clock_.AdvanceTime(interval);
      current_cwnd = cubic_.CongestionWindowAfterAck(
          kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
      const QuicByteCount expected_cwnd = CubicConvexCwndInBytes(
          initial_cwnd, rtt_min, (clock_.ApproximateNow() - initial_time));
      // If we allow per-ack updates, every update is a small cubic update.
      ASSERT_EQ(expected_cwnd, current_cwnd);
    }
  }
  const QuicByteCount expected_cwnd = CubicConvexCwndInBytes(
      initial_cwnd, rtt_min, (clock_.ApproximateNow() - initial_time));
  current_cwnd = cubic_.CongestionWindowAfterAck(
      kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  ASSERT_EQ(expected_cwnd, current_cwnd);
}

// TODO(ianswett): This test was disabled when all fixes were enabled, but it
// may be worth fixing.
TEST_F(CubicBytesTest, DISABLED_AboveOrigin) {
  // Convex growth.
  const QuicTime::Delta rtt_min = hundred_ms_;
  QuicByteCount current_cwnd = 10 * kDefaultTCPMSS;
  // Without the signed-integer, cubic-convex fix, we start out in the
  // wrong mode.
  QuicPacketCount expected_cwnd = RenoCwndInBytes(current_cwnd);
  // Initialize the state.
  clock_.AdvanceTime(one_ms_);
  ASSERT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd,
                                            rtt_min, clock_.ApproximateNow()));
  current_cwnd = expected_cwnd;
  const QuicPacketCount initial_cwnd = expected_cwnd;
  // Normal TCP phase.
  for (int i = 0; i < 48; ++i) {
    for (QuicPacketCount n = 1;
         n < current_cwnd / kDefaultTCPMSS / kNConnectionAlpha; ++n) {
      // Call once per ACK.
      ASSERT_NEAR(
          current_cwnd,
          cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd, rtt_min,
                                          clock_.ApproximateNow()),
          kDefaultTCPMSS);
    }
    clock_.AdvanceTime(hundred_ms_);
    current_cwnd = cubic_.CongestionWindowAfterAck(
        kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
    // When we fix convex mode and the uint64 arithmetic, we
    // increase the expected_cwnd only after after the first 100ms,
    // rather than after the initial 1ms.
    expected_cwnd += kDefaultTCPMSS;
    ASSERT_NEAR(expected_cwnd, current_cwnd, kDefaultTCPMSS);
  }
  // Cubic phase.
  for (int i = 0; i < 52; ++i) {
    for (QuicPacketCount n = 1; n < current_cwnd / kDefaultTCPMSS; ++n) {
      // Call once per ACK.
      ASSERT_NEAR(
          current_cwnd,
          cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd, rtt_min,
                                          clock_.ApproximateNow()),
          kDefaultTCPMSS);
    }
    clock_.AdvanceTime(hundred_ms_);
    current_cwnd = cubic_.CongestionWindowAfterAck(
        kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  }
  // Total time elapsed so far; add min_rtt (0.1s) here as well.
  float elapsed_time_s = 10.0f + 0.1f;
  // |expected_cwnd| is initial value of cwnd + K * t^3, where K = 0.4.
  expected_cwnd =
      initial_cwnd / kDefaultTCPMSS +
      (elapsed_time_s * elapsed_time_s * elapsed_time_s * 410) / 1024;
  EXPECT_EQ(expected_cwnd, current_cwnd / kDefaultTCPMSS);
}

// Constructs an artificial scenario to ensure that cubic-convex
// increases are truly fine-grained:
//
// - After starting the epoch, this test advances the elapsed time
// sufficiently far that cubic will do small increases at less than
// MaxCubicTimeInterval() intervals.
//
// - Sets an artificially large initial cwnd to prevent Reno from the
// convex increases on every ack.
TEST_F(CubicBytesTest, AboveOriginFineGrainedCubing) {
  // Start the test with an artificially large cwnd to prevent Reno
  // from over-taking cubic.
  QuicByteCount current_cwnd = 1000 * kDefaultTCPMSS;
  const QuicByteCount initial_cwnd = current_cwnd;
  const QuicTime::Delta rtt_min = hundred_ms_;
  clock_.AdvanceTime(one_ms_);
  QuicTime initial_time = clock_.ApproximateNow();

  // Start the epoch and then artificially advance the time.
  current_cwnd = cubic_.CongestionWindowAfterAck(
      kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(600));
  current_cwnd = cubic_.CongestionWindowAfterAck(
      kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());

  // We expect the algorithm to perform only non-zero, fine-grained cubic
  // increases on every ack in this case.
  for (int i = 0; i < 100; ++i) {
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
    const QuicByteCount expected_cwnd = CubicConvexCwndInBytes(
        initial_cwnd, rtt_min, (clock_.ApproximateNow() - initial_time));
    const QuicByteCount next_cwnd = cubic_.CongestionWindowAfterAck(
        kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
    // Make sure we are performing cubic increases.
    ASSERT_EQ(expected_cwnd, next_cwnd);
    // Make sure that these are non-zero, less-than-packet sized
    // increases.
    ASSERT_GT(next_cwnd, current_cwnd);
    const QuicByteCount cwnd_delta = next_cwnd - current_cwnd;
    ASSERT_GT(kDefaultTCPMSS * .1, cwnd_delta);

    current_cwnd = next_cwnd;
  }
}

// Constructs an artificial scenario to show what happens when we
// allow per-ack updates, rather than limititing update freqency.  In
// this scenario, the first two acks of the epoch produce the same
// cwnd.  When we limit per-ack updates, this would cause the
// cessation of cubic updates for 30ms.  When we allow per-ack
// updates, the window continues to grow on every ack.
TEST_F(CubicBytesTest, PerAckUpdates) {
  // Start the test with a large cwnd and RTT, to force the first
  // increase to be a cubic increase.
  QuicPacketCount initial_cwnd_packets = 150;
  QuicByteCount current_cwnd = initial_cwnd_packets * kDefaultTCPMSS;
  const QuicTime::Delta rtt_min = 350 * one_ms_;

  // Initialize the epoch
  clock_.AdvanceTime(one_ms_);
  // Keep track of the growth of the reno-equivalent cwnd.
  QuicByteCount reno_cwnd = RenoCwndInBytes(current_cwnd);
  current_cwnd = cubic_.CongestionWindowAfterAck(
      kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  const QuicByteCount initial_cwnd = current_cwnd;

  // Simulate the return of cwnd packets in less than
  // MaxCubicInterval() time.
  const QuicPacketCount max_acks = initial_cwnd_packets / kNConnectionAlpha;
  const QuicTime::Delta interval = QuicTime::Delta::FromMicroseconds(
      MaxCubicTimeInterval().ToMicroseconds() / (max_acks + 1));

  // In this scenario, the first increase is dictated by the cubic
  // equation, but it is less than one byte, so the cwnd doesn't
  // change.  Normally, without per-ack increases, any cwnd plateau
  // will cause the cwnd to be pinned for MaxCubicTimeInterval().  If
  // we enable per-ack updates, the cwnd will continue to grow,
  // regardless of the temporary plateau.
  clock_.AdvanceTime(interval);
  reno_cwnd = RenoCwndInBytes(reno_cwnd);
  ASSERT_EQ(current_cwnd,
            cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd,
                                            rtt_min, clock_.ApproximateNow()));
  for (QuicPacketCount i = 1; i < max_acks; ++i) {
    clock_.AdvanceTime(interval);
    const QuicByteCount next_cwnd = cubic_.CongestionWindowAfterAck(
        kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
    reno_cwnd = RenoCwndInBytes(reno_cwnd);
    // The window shoud increase on every ack.
    ASSERT_LT(current_cwnd, next_cwnd);
    ASSERT_EQ(reno_cwnd, next_cwnd);
    current_cwnd = next_cwnd;
  }

  // After all the acks are returned from the epoch, we expect the
  // cwnd to have increased by nearly one packet.  (Not exactly one
  // packet, because our byte-wise Reno algorithm is always a slight
  // under-estimation).  Without per-ack updates, the current_cwnd
  // would otherwise be unchanged.
  const QuicByteCount minimum_expected_increase = kDefaultTCPMSS * .9;
  EXPECT_LT(minimum_expected_increase + initial_cwnd, current_cwnd);
}

TEST_F(CubicBytesTest, LossEvents) {
  const QuicTime::Delta rtt_min = hundred_ms_;
  QuicByteCount current_cwnd = 422 * kDefaultTCPMSS;
  // Without the signed-integer, cubic-convex fix, we mistakenly
  // increment cwnd after only one_ms_ and a single ack.
  QuicPacketCount expected_cwnd = RenoCwndInBytes(current_cwnd);
  // Initialize the state.
  clock_.AdvanceTime(one_ms_);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd,
                                            rtt_min, clock_.ApproximateNow()));

  // On the first loss, the last max congestion window is set to the
  // congestion window before the loss.
  QuicByteCount pre_loss_cwnd = current_cwnd;
  ASSERT_EQ(0u, LastMaxCongestionWindow());
  expected_cwnd = static_cast<QuicByteCount>(current_cwnd * kNConnectionBeta);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
  ASSERT_EQ(pre_loss_cwnd, LastMaxCongestionWindow());
  current_cwnd = expected_cwnd;

  // On the second loss, the current congestion window has not yet
  // reached the last max congestion window.  The last max congestion
  // window will be reduced by an additional backoff factor to allow
  // for competition.
  pre_loss_cwnd = current_cwnd;
  expected_cwnd = static_cast<QuicByteCount>(current_cwnd * kNConnectionBeta);
  ASSERT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
  current_cwnd = expected_cwnd;
  EXPECT_GT(pre_loss_cwnd, LastMaxCongestionWindow());
  QuicByteCount expected_last_max =
      static_cast<QuicByteCount>(pre_loss_cwnd * kNConnectionBetaLastMax);
  EXPECT_EQ(expected_last_max, LastMaxCongestionWindow());
  EXPECT_LT(expected_cwnd, LastMaxCongestionWindow());
  // Simulate an increase, and check that we are below the origin.
  current_cwnd = cubic_.CongestionWindowAfterAck(
      kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  EXPECT_GT(LastMaxCongestionWindow(), current_cwnd);

  // On the final loss, simulate the condition where the congestion
  // window had a chance to grow nearly to the last congestion window.
  current_cwnd = LastMaxCongestionWindow() - 1;
  pre_loss_cwnd = current_cwnd;
  expected_cwnd = static_cast<QuicByteCount>(current_cwnd * kNConnectionBeta);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
  expected_last_max = pre_loss_cwnd;
  ASSERT_EQ(expected_last_max, LastMaxCongestionWindow());
}

TEST_F(CubicBytesTest, BelowOrigin) {
  // Concave growth.
  const QuicTime::Delta rtt_min = hundred_ms_;
  QuicByteCount current_cwnd = 422 * kDefaultTCPMSS;
  // Without the signed-integer, cubic-convex fix, we mistakenly
  // increment cwnd after only one_ms_ and a single ack.
  QuicPacketCount expected_cwnd = RenoCwndInBytes(current_cwnd);
  // Initialize the state.
  clock_.AdvanceTime(one_ms_);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterAck(kDefaultTCPMSS, current_cwnd,
                                            rtt_min, clock_.ApproximateNow()));
  expected_cwnd = static_cast<QuicPacketCount>(current_cwnd * kNConnectionBeta);
  EXPECT_EQ(expected_cwnd,
            cubic_.CongestionWindowAfterPacketLoss(current_cwnd));
  current_cwnd = expected_cwnd;
  // First update after loss to initialize the epoch.
  current_cwnd = cubic_.CongestionWindowAfterAck(
      kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  // Cubic phase.
  for (int i = 0; i < 40; ++i) {
    clock_.AdvanceTime(hundred_ms_);
    current_cwnd = cubic_.CongestionWindowAfterAck(
        kDefaultTCPMSS, current_cwnd, rtt_min, clock_.ApproximateNow());
  }
  expected_cwnd = 553632;
  EXPECT_EQ(expected_cwnd, current_cwnd);
}

}  // namespace test
}  // namespace quic
