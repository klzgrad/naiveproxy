// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_network_blackhole_detector.h"

#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

class QuicNetworkBlackholeDetectorPeer {
 public:
  static QuicAlarm* GetAlarm(QuicNetworkBlackholeDetector* detector) {
    return detector->alarm_.get();
  }
};

namespace {
class MockDelegate : public QuicNetworkBlackholeDetector::Delegate {
 public:
  MOCK_METHOD0(OnPathDegradingDetected, void());
  MOCK_METHOD0(OnBlackholeDetected, void());
};

const size_t kPathDegradingDelayInSeconds = 5;
const size_t kBlackholeDelayInSeconds = 10;

class QuicNetworkBlackholeDetectorTest : public QuicTest {
 public:
  QuicNetworkBlackholeDetectorTest()
      : detector_(&delegate_, &arena_, &alarm_factory_),
        alarm_(static_cast<MockAlarmFactory::TestAlarm*>(
            QuicNetworkBlackholeDetectorPeer::GetAlarm(&detector_))),
        path_degrading_delay_(
            QuicTime::Delta::FromSeconds(kPathDegradingDelayInSeconds)),
        blackhole_delay_(
            QuicTime::Delta::FromSeconds(kBlackholeDelayInSeconds)) {
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }

 protected:
  testing::StrictMock<MockDelegate> delegate_;
  QuicConnectionArena arena_;
  MockAlarmFactory alarm_factory_;

  QuicNetworkBlackholeDetector detector_;

  MockAlarmFactory::TestAlarm* alarm_;
  MockClock clock_;
  const QuicTime::Delta path_degrading_delay_;
  const QuicTime::Delta blackhole_delay_;
};

TEST_F(QuicNetworkBlackholeDetectorTest, StartAndFire) {
  EXPECT_FALSE(detector_.IsDetectionInProgress());

  detector_.RestartDetection(clock_.Now() + path_degrading_delay_,
                             clock_.Now() + blackhole_delay_);
  EXPECT_TRUE(detector_.IsDetectionInProgress());
  EXPECT_EQ(clock_.Now() + path_degrading_delay_, alarm_->deadline());

  // Fire path degrading alarm.
  clock_.AdvanceTime(path_degrading_delay_);
  EXPECT_CALL(delegate_, OnPathDegradingDetected());
  alarm_->Fire();
  // Verify blackhole detection is still in progress.
  EXPECT_TRUE(detector_.IsDetectionInProgress());
  EXPECT_EQ(clock_.Now() + blackhole_delay_ - path_degrading_delay_,
            alarm_->deadline());

  // Fire blackhole detection alarm.
  clock_.AdvanceTime(blackhole_delay_ - path_degrading_delay_);
  EXPECT_CALL(delegate_, OnBlackholeDetected());
  alarm_->Fire();
  EXPECT_FALSE(detector_.IsDetectionInProgress());
}

TEST_F(QuicNetworkBlackholeDetectorTest, RestartAndStop) {
  detector_.RestartDetection(clock_.Now() + path_degrading_delay_,
                             clock_.Now() + blackhole_delay_);

  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  detector_.RestartDetection(clock_.Now() + path_degrading_delay_,
                             clock_.Now() + blackhole_delay_);
  EXPECT_EQ(clock_.Now() + path_degrading_delay_, alarm_->deadline());

  detector_.StopDetection();
  EXPECT_FALSE(detector_.IsDetectionInProgress());
}

TEST_F(QuicNetworkBlackholeDetectorTest, PathDegradingFiresAndRestart) {
  EXPECT_FALSE(detector_.IsDetectionInProgress());
  detector_.RestartDetection(clock_.Now() + path_degrading_delay_,
                             clock_.Now() + blackhole_delay_);
  EXPECT_TRUE(detector_.IsDetectionInProgress());
  EXPECT_EQ(clock_.Now() + path_degrading_delay_, alarm_->deadline());

  // Fire path degrading alarm.
  clock_.AdvanceTime(path_degrading_delay_);
  EXPECT_CALL(delegate_, OnPathDegradingDetected());
  alarm_->Fire();
  // Verify blackhole detection is still in progress.
  EXPECT_TRUE(detector_.IsDetectionInProgress());
  EXPECT_EQ(clock_.Now() + blackhole_delay_ - path_degrading_delay_,
            alarm_->deadline());

  // After 100ms, restart detections on forward progress.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(100));
  detector_.RestartDetection(clock_.Now() + path_degrading_delay_,
                             clock_.Now() + blackhole_delay_);
  // Verify alarm is armed based on path degrading deadline.
  EXPECT_EQ(clock_.Now() + path_degrading_delay_, alarm_->deadline());
}

}  // namespace

}  // namespace test
}  // namespace quic
