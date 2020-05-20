// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_idle_network_detector.h"

#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

class QuicIdleNetworkDetectorTestPeer {
 public:
  static QuicAlarm* GetAlarm(QuicIdleNetworkDetector* detector) {
    return detector->alarm_.get();
  }
};

namespace {

class MockDelegate : public QuicIdleNetworkDetector::Delegate {
 public:
  MOCK_METHOD0(OnHandshakeTimeout, void());
  MOCK_METHOD0(OnIdleNetworkDetected, void());
};

class QuicIdleNetworkDetectorTest : public QuicTest {
 public:
  QuicIdleNetworkDetectorTest() {
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
    detector_ = std::make_unique<QuicIdleNetworkDetector>(
        &delegate_, clock_.Now(), &arena_, &alarm_factory_);
    alarm_ = static_cast<MockAlarmFactory::TestAlarm*>(
        QuicIdleNetworkDetectorTestPeer::GetAlarm(detector_.get()));
  }

 protected:
  testing::StrictMock<MockDelegate> delegate_;
  QuicConnectionArena arena_;
  MockAlarmFactory alarm_factory_;

  std::unique_ptr<QuicIdleNetworkDetector> detector_;

  MockAlarmFactory::TestAlarm* alarm_;
  MockClock clock_;
};

TEST_F(QuicIdleNetworkDetectorTest,
       IdleNetworkDetectedBeforeHandshakeCompletes) {
  EXPECT_FALSE(alarm_->IsSet());
  detector_->SetTimeouts(
      /*handshake_timeout=*/QuicTime::Delta::FromSeconds(30),
      /*idle_network_timeout=*/QuicTime::Delta::FromSeconds(20));
  EXPECT_TRUE(alarm_->IsSet());
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromSeconds(20),
            alarm_->deadline());

  // No network activity for 20s.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(20));
  EXPECT_CALL(delegate_, OnIdleNetworkDetected());
  alarm_->Fire();
}

TEST_F(QuicIdleNetworkDetectorTest, HandshakeTimeout) {
  EXPECT_FALSE(alarm_->IsSet());
  detector_->SetTimeouts(
      /*handshake_timeout=*/QuicTime::Delta::FromSeconds(30),
      /*idle_network_timeout=*/QuicTime::Delta::FromSeconds(20));
  EXPECT_TRUE(alarm_->IsSet());

  // Has network activity after 15s.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(15));
  detector_->OnPacketReceived(clock_.Now());
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromSeconds(15),
            alarm_->deadline());
  // Handshake does not complete for another 15s.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(15));
  EXPECT_CALL(delegate_, OnHandshakeTimeout());
  alarm_->Fire();
}

TEST_F(QuicIdleNetworkDetectorTest,
       IdleNetworkDetectedAfterHandshakeCompletes) {
  EXPECT_FALSE(alarm_->IsSet());
  detector_->SetTimeouts(
      /*handshake_timeout=*/QuicTime::Delta::FromSeconds(30),
      /*idle_network_timeout=*/QuicTime::Delta::FromSeconds(20));
  EXPECT_TRUE(alarm_->IsSet());

  // Handshake completes in 200ms.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(200));
  detector_->OnPacketReceived(clock_.Now());
  detector_->SetTimeouts(
      /*handshake_timeout=*/QuicTime::Delta::Infinite(),
      /*idle_network_timeout=*/QuicTime::Delta::FromSeconds(600));
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromSeconds(600),
            alarm_->deadline());

  // No network activity for 600s.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(600));
  EXPECT_CALL(delegate_, OnIdleNetworkDetected());
  alarm_->Fire();
}

TEST_F(QuicIdleNetworkDetectorTest,
       DoNotExtendIdleDeadlineOnConsecutiveSentPackets) {
  EXPECT_FALSE(alarm_->IsSet());
  detector_->SetTimeouts(
      /*handshake_timeout=*/QuicTime::Delta::FromSeconds(30),
      /*idle_network_timeout=*/QuicTime::Delta::FromSeconds(20));
  EXPECT_TRUE(alarm_->IsSet());

  // Handshake completes in 200ms.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(200));
  detector_->OnPacketReceived(clock_.Now());
  detector_->SetTimeouts(
      /*handshake_timeout=*/QuicTime::Delta::Infinite(),
      /*idle_network_timeout=*/QuicTime::Delta::FromSeconds(600));
  EXPECT_EQ(clock_.Now() + QuicTime::Delta::FromSeconds(600),
            alarm_->deadline());

  // Sent packets after 200ms.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(200));
  detector_->OnPacketSent(clock_.Now());
  const QuicTime packet_sent_time = clock_.Now();
  EXPECT_EQ(packet_sent_time + QuicTime::Delta::FromSeconds(600),
            alarm_->deadline());

  // Sent another packet after 200ms
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(200));
  detector_->OnPacketSent(clock_.Now());
  // Verify idle network deadline does not extend.
  EXPECT_EQ(packet_sent_time + QuicTime::Delta::FromSeconds(600),
            alarm_->deadline());

  // No network activity for 600s.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(600) -
                     QuicTime::Delta::FromMilliseconds(200));
  EXPECT_CALL(delegate_, OnIdleNetworkDetected());
  alarm_->Fire();
}

}  // namespace

}  // namespace test
}  // namespace quic
