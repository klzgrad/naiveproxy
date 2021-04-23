// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_connection_id_manager.h"
#include <cstddef>

#include "quic/core/quic_connection_id.h"
#include "quic/core/quic_error_codes.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/mock_clock.h"
#include "quic/test_tools/quic_test_utils.h"

namespace quic {

class QuicConnectionIdManagerPeer {
 public:
  static QuicAlarm* GetRetirePeerIssuedConnectionIdAlarm(
      QuicPeerIssuedConnectionIdManager* manager) {
    return manager->retire_connection_id_alarm_.get();
  }

  static QuicAlarm* GetRetireSelfIssuedConnectionIdAlarm(
      QuicSelfIssuedConnectionIdManager* manager) {
    return manager->retire_connection_id_alarm_.get();
  }
};

namespace {

using ::quic::test::IsError;
using ::quic::test::IsQuicNoError;
using ::quic::test::TestConnectionId;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::IsNull;
using ::testing::Return;
using ::testing::StrictMock;

class TestPeerIssuedConnectionIdManagerVisitor
    : public QuicConnectionIdManagerVisitorInterface {
 public:
  void SetPeerIssuedConnectionIdManager(
      QuicPeerIssuedConnectionIdManager* peer_issued_connection_id_manager) {
    peer_issued_connection_id_manager_ = peer_issued_connection_id_manager;
  }

  void OnPeerIssuedConnectionIdRetired() override {
    // Replace current connection Id if it has been retired.
    if (!peer_issued_connection_id_manager_->IsConnectionIdActive(
            current_peer_issued_connection_id_)) {
      current_peer_issued_connection_id_ =
          peer_issued_connection_id_manager_->ConsumeOneUnusedConnectionId()
              ->connection_id;
    }
    // Retire all the to-be-retired connection Ids.
    most_recent_retired_connection_id_sequence_numbers_ =
        peer_issued_connection_id_manager_
            ->ConsumeToBeRetiredConnectionIdSequenceNumbers();
  }

  const std::vector<uint64_t>&
  most_recent_retired_connection_id_sequence_numbers() {
    return most_recent_retired_connection_id_sequence_numbers_;
  }

  void SetCurrentPeerConnectionId(QuicConnectionId cid) {
    current_peer_issued_connection_id_ = cid;
  }

  const QuicConnectionId& GetCurrentPeerConnectionId() {
    return current_peer_issued_connection_id_;
  }

  bool SendNewConnectionId(const QuicNewConnectionIdFrame& /*frame*/) override {
    return false;
  }
  void OnNewConnectionIdIssued(
      const QuicConnectionId& /*connection_id*/) override {}
  void OnSelfIssuedConnectionIdRetired(
      const QuicConnectionId& /*connection_id*/) override {}

 private:
  QuicPeerIssuedConnectionIdManager* peer_issued_connection_id_manager_ =
      nullptr;
  QuicConnectionId current_peer_issued_connection_id_;
  std::vector<uint64_t> most_recent_retired_connection_id_sequence_numbers_;
};

class QuicPeerIssuedConnectionIdManagerTest : public QuicTest {
 public:
  QuicPeerIssuedConnectionIdManagerTest()
      : peer_issued_cid_manager_(/*active_connection_id_limit=*/2,
                                 initial_connection_id_,
                                 &clock_,
                                 &alarm_factory_,
                                 &cid_manager_visitor_) {
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
    cid_manager_visitor_.SetPeerIssuedConnectionIdManager(
        &peer_issued_cid_manager_);
    cid_manager_visitor_.SetCurrentPeerConnectionId(initial_connection_id_);
    retire_peer_issued_cid_alarm_ =
        QuicConnectionIdManagerPeer::GetRetirePeerIssuedConnectionIdAlarm(
            &peer_issued_cid_manager_);
  }

 protected:
  MockClock clock_;
  test::MockAlarmFactory alarm_factory_;
  TestPeerIssuedConnectionIdManagerVisitor cid_manager_visitor_;
  QuicConnectionId initial_connection_id_ = TestConnectionId(0);
  QuicPeerIssuedConnectionIdManager peer_issued_cid_manager_;
  QuicAlarm* retire_peer_issued_cid_alarm_ = nullptr;
  std::string error_details_;
};

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       ConnectionIdSequenceWhenMigrationSucceed) {
  {
    // Receives CID #1 from peer.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());

    // Start to use CID #1 for alternative path.
    const QuicConnectionIdData* aternative_connection_id_data =
        peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
    ASSERT_THAT(aternative_connection_id_data, testing::NotNull());
    EXPECT_EQ(aternative_connection_id_data->connection_id,
              TestConnectionId(1));
    EXPECT_EQ(aternative_connection_id_data->stateless_reset_token,
              frame.stateless_reset_token);

    // Connection migration succeed. Prepares to retire CID #0.
    peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
        TestConnectionId(0));
    cid_manager_visitor_.SetCurrentPeerConnectionId(TestConnectionId(1));
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(0u));
  }

  {
    // Receives CID #2 from peer since CID #0 is retired.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(2);
    frame.sequence_number = 2u;
    frame.retire_prior_to = 1u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    // Start to use CID #2 for alternative path.
    peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
    // Connection migration succeed. Prepares to retire CID #1.
    peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
        TestConnectionId(1));
    cid_manager_visitor_.SetCurrentPeerConnectionId(TestConnectionId(2));
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(1u));
  }

  {
    // Receives CID #3 from peer since CID #1 is retired.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(3);
    frame.sequence_number = 3u;
    frame.retire_prior_to = 2u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    // Start to use CID #3 for alternative path.
    peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
    // Connection migration succeed. Prepares to retire CID #2.
    peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
        TestConnectionId(2));
    cid_manager_visitor_.SetCurrentPeerConnectionId(TestConnectionId(3));
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(2u));
  }

  {
    // Receives CID #4 from peer since CID #2 is retired.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(4);
    frame.sequence_number = 4u;
    frame.retire_prior_to = 3u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       ConnectionIdSequenceWhenMigrationFail) {
  {
    // Receives CID #1 from peer.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    // Start to use CID #1 for alternative path.
    peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
    // Connection migration fails. Prepares to retire CID #1.
    peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
        TestConnectionId(1));
    // Actually retires CID #1.
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(1u));
  }

  {
    // Receives CID #2 from peer since CID #1 is retired.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(2);
    frame.sequence_number = 2u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    // Start to use CID #2 for alternative path.
    peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
    // Connection migration fails again. Prepares to retire CID #2.
    peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
        TestConnectionId(2));
    // Actually retires CID #2.
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(2u));
  }

  {
    // Receives CID #3 from peer since CID #2 is retired.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(3);
    frame.sequence_number = 3u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    // Start to use CID #3 for alternative path.
    peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
    // Connection migration succeed. Prepares to retire CID #0.
    peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
        TestConnectionId(0));
    // After CID #3 is default (i.e., when there is no pending frame to write
    // associated with CID #0), #0 can actually be retired.
    cid_manager_visitor_.SetCurrentPeerConnectionId(TestConnectionId(3));
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(0u));
  }

  {
    // Receives CID #4 from peer since CID #0 is retired.
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(4);
    frame.sequence_number = 4u;
    frame.retire_prior_to = 3u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    EXPECT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    EXPECT_FALSE(retire_peer_issued_cid_alarm_->IsSet());
  }
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       ReceivesNewConnectionIdOutOfOrder) {
  {
    // Receives new CID #1 that retires prior to #0.
    // Outcome: (active: #0 unused: #1)
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    // Start to use CID #1 for alternative path.
    // Outcome: (active: #0 #1 unused: None)
    peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
  }

  {
    // Receives new CID #3 that retires prior to #2.
    // Outcome: (active: None unused: #3)
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(3);
    frame.sequence_number = 3u;
    frame.retire_prior_to = 2u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }

  {
    // Receives new CID #2 that retires prior to #1.
    // Outcome: (active: None unused: #3, #2)
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(2);
    frame.sequence_number = 2u;
    frame.retire_prior_to = 1u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }

  {
    EXPECT_FALSE(
        peer_issued_cid_manager_.IsConnectionIdActive(TestConnectionId(0)));
    EXPECT_FALSE(
        peer_issued_cid_manager_.IsConnectionIdActive(TestConnectionId(1)));
    // When there is no frame associated with #0 and #1 to write, replace the
    // in-use CID with an unused CID (#2) and retires #0 & #1.
    ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
    alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_visitor_
                    .most_recent_retired_connection_id_sequence_numbers(),
                ElementsAre(0u, 1u));
    EXPECT_EQ(cid_manager_visitor_.GetCurrentPeerConnectionId(),
              TestConnectionId(2));
    // Get another unused CID for path validation.
    EXPECT_EQ(
        peer_issued_cid_manager_.ConsumeOneUnusedConnectionId()->connection_id,
        TestConnectionId(3));
  }
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       VisitedNewConnectionIdFrameIsIgnored) {
  // Receives new CID #1 that retires prior to #0.
  // Outcome: (active: #0 unused: #1)
  QuicNewConnectionIdFrame frame;
  frame.connection_id = TestConnectionId(1);
  frame.sequence_number = 1u;
  frame.retire_prior_to = 0u;
  frame.stateless_reset_token =
      QuicUtils::GenerateStatelessResetToken(frame.connection_id);
  ASSERT_THAT(
      peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
      IsQuicNoError());
  // Start to use CID #1 for alternative path.
  // Outcome: (active: #0 #1 unused: None)
  peer_issued_cid_manager_.ConsumeOneUnusedConnectionId();
  // Prepare to retire CID #1 as path validation fails.
  peer_issued_cid_manager_.PrepareToRetireActiveConnectionId(
      TestConnectionId(1));
  // Actually retires CID #1.
  ASSERT_TRUE(retire_peer_issued_cid_alarm_->IsSet());
  alarm_factory_.FireAlarm(retire_peer_issued_cid_alarm_);
  EXPECT_THAT(
      cid_manager_visitor_.most_recent_retired_connection_id_sequence_numbers(),
      ElementsAre(1u));
  // Receives the same frame again. Should be a no-op.
  ASSERT_THAT(
      peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
      IsQuicNoError());
  EXPECT_THAT(peer_issued_cid_manager_.ConsumeOneUnusedConnectionId(),
              testing::IsNull());
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       ErrorWhenActiveConnectionIdLimitExceeded) {
  {
    // Receives new CID #1 that retires prior to #0.
    // Outcome: (active: #0 unused: #1)
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }

  {
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(2);
    frame.sequence_number = 2u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsError(QUIC_CONNECTION_ID_LIMIT_ERROR));
  }
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       ErrorWhenTheSameConnectionIdIsSeenWithDifferentSequenceNumbers) {
  {
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }

  {
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 2u;
    frame.retire_prior_to = 1u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(TestConnectionId(2));
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsError(IETF_QUIC_PROTOCOL_VIOLATION));
  }
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       NewConnectionIdFrameWithTheSameSequenceNumberIsIgnored) {
  {
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(1);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }

  {
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(2);
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(TestConnectionId(2));
    EXPECT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
    EXPECT_EQ(
        peer_issued_cid_manager_.ConsumeOneUnusedConnectionId()->connection_id,
        TestConnectionId(1));
    EXPECT_THAT(peer_issued_cid_manager_.ConsumeOneUnusedConnectionId(),
                IsNull());
  }
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest,
       ErrorWhenThereAreTooManyGapsInIssuedConnectionIdSequenceNumbers) {
  // Add 20 intervals: [0, 1), [2, 3), ..., [38,39)
  for (int i = 2; i <= 38; i += 2) {
    QuicNewConnectionIdFrame frame;
    frame.connection_id = TestConnectionId(i);
    frame.sequence_number = i;
    frame.retire_prior_to = i;
    frame.stateless_reset_token =
        QuicUtils::GenerateStatelessResetToken(frame.connection_id);
    ASSERT_THAT(
        peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
        IsQuicNoError());
  }

  // Interval [40, 41) goes over the limit.
  QuicNewConnectionIdFrame frame;
  frame.connection_id = TestConnectionId(40);
  frame.sequence_number = 40u;
  frame.retire_prior_to = 40u;
  frame.stateless_reset_token =
      QuicUtils::GenerateStatelessResetToken(frame.connection_id);
  ASSERT_THAT(
      peer_issued_cid_manager_.OnNewConnectionIdFrame(frame, &error_details_),
      IsError(IETF_QUIC_PROTOCOL_VIOLATION));
}

TEST_F(QuicPeerIssuedConnectionIdManagerTest, ReplaceConnectionId) {
  ASSERT_TRUE(
      peer_issued_cid_manager_.IsConnectionIdActive(initial_connection_id_));
  peer_issued_cid_manager_.ReplaceConnectionId(initial_connection_id_,
                                               TestConnectionId(1));
  EXPECT_FALSE(
      peer_issued_cid_manager_.IsConnectionIdActive(initial_connection_id_));
  EXPECT_TRUE(
      peer_issued_cid_manager_.IsConnectionIdActive(TestConnectionId(1)));
}

class TestSelfIssuedConnectionIdManagerVisitor
    : public QuicConnectionIdManagerVisitorInterface {
 public:
  void OnPeerIssuedConnectionIdRetired() override {}

  MOCK_METHOD(bool,
              SendNewConnectionId,
              (const QuicNewConnectionIdFrame& frame),
              (override));
  MOCK_METHOD(void,
              OnNewConnectionIdIssued,
              (const QuicConnectionId& connection_id),
              (override));
  MOCK_METHOD(void,
              OnSelfIssuedConnectionIdRetired,
              (const QuicConnectionId& connection_id),
              (override));
};

class QuicSelfIssuedConnectionIdManagerTest : public QuicTest {
 public:
  QuicSelfIssuedConnectionIdManagerTest()
      : cid_manager_(/*active_connection_id_limit*/ 2,
                     initial_connection_id_,
                     &clock_,
                     &alarm_factory_,
                     &cid_manager_visitor_) {
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
    retire_self_issued_cid_alarm_ =
        QuicConnectionIdManagerPeer::GetRetireSelfIssuedConnectionIdAlarm(
            &cid_manager_);
  }

 protected:
  MockClock clock_;
  test::MockAlarmFactory alarm_factory_;
  TestSelfIssuedConnectionIdManagerVisitor cid_manager_visitor_;
  QuicConnectionId initial_connection_id_ = TestConnectionId(0);
  StrictMock<QuicSelfIssuedConnectionIdManager> cid_manager_;
  QuicAlarm* retire_self_issued_cid_alarm_ = nullptr;
  std::string error_details_;
  QuicTime::Delta pto_delay_ = QuicTime::Delta::FromMilliseconds(10);
};

MATCHER_P3(ExpectedNewConnectionIdFrame,
           connection_id,
           sequence_number,
           retire_prior_to,
           "") {
  return (arg.connection_id == connection_id) &&
         (arg.sequence_number == sequence_number) &&
         (arg.retire_prior_to == retire_prior_to);
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       RetireSelfIssuedConnectionIdInOrder) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);
  QuicConnectionId cid2 = cid_manager_.GenerateNewConnectionId(cid1);
  QuicConnectionId cid3 = cid_manager_.GenerateNewConnectionId(cid2);
  QuicConnectionId cid4 = cid_manager_.GenerateNewConnectionId(cid3);
  QuicConnectionId cid5 = cid_manager_.GenerateNewConnectionId(cid4);

  // Sends CID #1 to peer.
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid1));
  EXPECT_CALL(cid_manager_visitor_,
              SendNewConnectionId(ExpectedNewConnectionIdFrame(cid1, 1u, 0u)))
      .WillOnce(Return(true));
  cid_manager_.MaybeSendNewConnectionIds();

  {
    // Peer retires CID #0;
    // Sends CID #2 and asks peer to retire CIDs prior to #1.
    // Outcome: (#1, #2) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid2));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid2, 2u, 1u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 0u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #1;
    // Sends CID #3 and asks peer to retire CIDs prior to #2.
    // Outcome: (#2, #3) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid3));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid3, 3u, 2u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 1u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #2;
    // Sends CID #4 and asks peer to retire CIDs prior to #3.
    // Outcome: (#3, #4) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid4));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid4, 4u, 3u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 2u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #3;
    // Sends CID #5 and asks peer to retire CIDs prior to #4.
    // Outcome: (#4, #5) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid5));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid5, 5u, 4u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 3u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       RetireSelfIssuedConnectionIdOutOfOrder) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);
  QuicConnectionId cid2 = cid_manager_.GenerateNewConnectionId(cid1);
  QuicConnectionId cid3 = cid_manager_.GenerateNewConnectionId(cid2);
  QuicConnectionId cid4 = cid_manager_.GenerateNewConnectionId(cid3);

  // Sends CID #1 to peer.
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid1));
  EXPECT_CALL(cid_manager_visitor_,
              SendNewConnectionId(ExpectedNewConnectionIdFrame(cid1, 1u, 0u)))
      .WillOnce(Return(true));
  cid_manager_.MaybeSendNewConnectionIds();

  {
    // Peer retires CID #1;
    // Sends CID #2 and asks peer to retire CIDs prior to #0.
    // Outcome: (#0, #2) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid2));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid2, 2u, 0u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 1u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #1 again. This is a no-op.
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 1u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #0;
    // Sends CID #3 and asks peer to retire CIDs prior to #2.
    // Outcome: (#2, #3) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid3));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid3, 3u, 2u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 0u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #3;
    // Sends CID #4 and asks peer to retire CIDs prior to #2.
    // Outcome: (#2, #4) are active.
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid4));
    EXPECT_CALL(cid_manager_visitor_,
                SendNewConnectionId(ExpectedNewConnectionIdFrame(cid4, 4u, 2u)))
        .WillOnce(Return(true));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 3u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }

  {
    // Peer retires CID #0 again. This is a no-op.
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = 0u;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
  }
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       ScheduleConnectionIdRetirementOneAtATime) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);
  QuicConnectionId cid2 = cid_manager_.GenerateNewConnectionId(cid1);
  QuicConnectionId cid3 = cid_manager_.GenerateNewConnectionId(cid2);
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_)).Times(3);
  EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_))
      .Times(3)
      .WillRepeatedly(Return(true));
  QuicTime::Delta connection_id_expire_timeout = 3 * pto_delay_;
  QuicRetireConnectionIdFrame retire_cid_frame;

  // CID #1 is sent to peer.
  cid_manager_.MaybeSendNewConnectionIds();

  // CID #0's retirement is scheduled and CID #2 is sent to peer.
  retire_cid_frame.sequence_number = 0u;
  ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                  retire_cid_frame, pto_delay_, &error_details_),
              IsQuicNoError());
  // While CID #0's retirement is scheduled, it is not retired yet.
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid0, cid1, cid2));
  EXPECT_TRUE(retire_self_issued_cid_alarm_->IsSet());
  EXPECT_EQ(retire_self_issued_cid_alarm_->deadline(),
            clock_.ApproximateNow() + connection_id_expire_timeout);

  // CID #0 is actually retired.
  EXPECT_CALL(cid_manager_visitor_, OnSelfIssuedConnectionIdRetired(cid0));
  clock_.AdvanceTime(connection_id_expire_timeout);
  alarm_factory_.FireAlarm(retire_self_issued_cid_alarm_);
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid1, cid2));
  EXPECT_FALSE(retire_self_issued_cid_alarm_->IsSet());

  // CID #1's retirement is scheduled and CID #3 is sent to peer.
  retire_cid_frame.sequence_number = 1u;
  ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                  retire_cid_frame, pto_delay_, &error_details_),
              IsQuicNoError());
  // While CID #1's retirement is scheduled, it is not retired yet.
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid1, cid2, cid3));
  EXPECT_TRUE(retire_self_issued_cid_alarm_->IsSet());
  EXPECT_EQ(retire_self_issued_cid_alarm_->deadline(),
            clock_.ApproximateNow() + connection_id_expire_timeout);

  // CID #1 is actually retired.
  EXPECT_CALL(cid_manager_visitor_, OnSelfIssuedConnectionIdRetired(cid1));
  clock_.AdvanceTime(connection_id_expire_timeout);
  alarm_factory_.FireAlarm(retire_self_issued_cid_alarm_);
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid2, cid3));
  EXPECT_FALSE(retire_self_issued_cid_alarm_->IsSet());
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       ScheduleMultipleConnectionIdRetirement) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);
  QuicConnectionId cid2 = cid_manager_.GenerateNewConnectionId(cid1);
  QuicConnectionId cid3 = cid_manager_.GenerateNewConnectionId(cid2);
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_)).Times(3);
  EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_))
      .Times(3)
      .WillRepeatedly(Return(true));
  QuicTime::Delta connection_id_expire_timeout = 3 * pto_delay_;
  QuicRetireConnectionIdFrame retire_cid_frame;

  // CID #1 is sent to peer.
  cid_manager_.MaybeSendNewConnectionIds();

  // CID #0's retirement is scheduled and CID #2 is sent to peer.
  retire_cid_frame.sequence_number = 0u;
  ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                  retire_cid_frame, pto_delay_, &error_details_),
              IsQuicNoError());

  clock_.AdvanceTime(connection_id_expire_timeout * 0.25);

  // CID #1's retirement is scheduled and CID #3 is sent to peer.
  retire_cid_frame.sequence_number = 1u;
  ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                  retire_cid_frame, pto_delay_, &error_details_),
              IsQuicNoError());

  // While CID #0, #1s retirement is scheduled, they are not retired yet.
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid0, cid1, cid2, cid3));
  EXPECT_TRUE(retire_self_issued_cid_alarm_->IsSet());
  EXPECT_EQ(retire_self_issued_cid_alarm_->deadline(),
            clock_.ApproximateNow() + connection_id_expire_timeout * 0.75);

  // CID #0 is actually retired.
  EXPECT_CALL(cid_manager_visitor_, OnSelfIssuedConnectionIdRetired(cid0));
  clock_.AdvanceTime(connection_id_expire_timeout * 0.75);
  alarm_factory_.FireAlarm(retire_self_issued_cid_alarm_);
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid1, cid2, cid3));
  EXPECT_TRUE(retire_self_issued_cid_alarm_->IsSet());
  EXPECT_EQ(retire_self_issued_cid_alarm_->deadline(),
            clock_.ApproximateNow() + connection_id_expire_timeout * 0.25);

  // CID #1 is actually retired.
  EXPECT_CALL(cid_manager_visitor_, OnSelfIssuedConnectionIdRetired(cid1));
  clock_.AdvanceTime(connection_id_expire_timeout * 0.25);
  alarm_factory_.FireAlarm(retire_self_issued_cid_alarm_);
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid2, cid3));
  EXPECT_FALSE(retire_self_issued_cid_alarm_->IsSet());
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       AllExpiredConnectionIdsAreRetiredInOneBatch) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);
  QuicConnectionId cid2 = cid_manager_.GenerateNewConnectionId(cid1);
  QuicConnectionId cid3 = cid_manager_.GenerateNewConnectionId(cid2);
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_)).Times(3);
  EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_))
      .Times(3)
      .WillRepeatedly(Return(true));
  QuicTime::Delta connection_id_expire_timeout = 3 * pto_delay_;
  QuicRetireConnectionIdFrame retire_cid_frame;

  // CID #1 is sent to peer.
  cid_manager_.MaybeSendNewConnectionIds();

  // CID #0's retirement is scheduled and CID #2 is sent to peer.
  retire_cid_frame.sequence_number = 0u;
  cid_manager_.OnRetireConnectionIdFrame(retire_cid_frame, pto_delay_,
                                         &error_details_);

  clock_.AdvanceTime(connection_id_expire_timeout * 0.1);

  // CID #1's retirement is scheduled and CID #3 is sent to peer.
  retire_cid_frame.sequence_number = 1u;
  cid_manager_.OnRetireConnectionIdFrame(retire_cid_frame, pto_delay_,
                                         &error_details_);

  {
    // CID #0 & #1 are retired in a single alarm fire.
    clock_.AdvanceTime(connection_id_expire_timeout);
    testing::InSequence s;
    EXPECT_CALL(cid_manager_visitor_, OnSelfIssuedConnectionIdRetired(cid0));
    EXPECT_CALL(cid_manager_visitor_, OnSelfIssuedConnectionIdRetired(cid1));
    alarm_factory_.FireAlarm(retire_self_issued_cid_alarm_);
    EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
                ElementsAre(cid2, cid3));
    EXPECT_FALSE(retire_self_issued_cid_alarm_->IsSet());
  }
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       ErrorWhenRetireConnectionIdNeverIssued) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);

  // CID #1 is sent to peer.
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_)).Times(1);
  EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_))
      .WillOnce(Return(true));
  cid_manager_.MaybeSendNewConnectionIds();

  // CID #2 is never issued.
  QuicRetireConnectionIdFrame retire_cid_frame;
  retire_cid_frame.sequence_number = 2u;
  ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                  retire_cid_frame, pto_delay_, &error_details_),
              IsError(IETF_QUIC_PROTOCOL_VIOLATION));
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       ErrorWhenTooManyConnectionIdWaitingToBeRetired) {
  // CID #0 & #1 are issued.
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_));
  EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_))
      .WillOnce(Return(true));
  cid_manager_.MaybeSendNewConnectionIds();

  // Add 8 connection IDs to the to-be-retired list.
  QuicConnectionId last_connection_id =
      cid_manager_.GenerateNewConnectionId(initial_connection_id_);
  for (int i = 0; i < 8; ++i) {
    EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_));
    EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_));
    QuicRetireConnectionIdFrame retire_cid_frame;
    retire_cid_frame.sequence_number = i;
    ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                    retire_cid_frame, pto_delay_, &error_details_),
                IsQuicNoError());
    last_connection_id =
        cid_manager_.GenerateNewConnectionId(last_connection_id);
  }
  QuicRetireConnectionIdFrame retire_cid_frame;
  retire_cid_frame.sequence_number = 8u;
  // This would have push the number of to-be-retired connection IDs over its
  // limit.
  ASSERT_THAT(cid_manager_.OnRetireConnectionIdFrame(
                  retire_cid_frame, pto_delay_, &error_details_),
              IsError(QUIC_TOO_MANY_CONNECTION_ID_WAITING_TO_RETIRE));
}

TEST_F(QuicSelfIssuedConnectionIdManagerTest,
       DoNotIssueConnectionIdVoluntarilyIfOneHasIssuedForPerferredAddress) {
  QuicConnectionId cid0 = initial_connection_id_;
  QuicConnectionId cid1 = cid_manager_.GenerateNewConnectionId(cid0);
  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(cid1));
  ASSERT_THAT(cid_manager_.IssueNewConnectionIdForPreferredAddress(),
              ExpectedNewConnectionIdFrame(cid1, 1u, 0u));
  EXPECT_THAT(cid_manager_.GetUnretiredConnectionIds(),
              ElementsAre(cid0, cid1));

  EXPECT_CALL(cid_manager_visitor_, OnNewConnectionIdIssued(_)).Times(0);
  EXPECT_CALL(cid_manager_visitor_, SendNewConnectionId(_)).Times(0);
  cid_manager_.MaybeSendNewConnectionIds();
}

}  // namespace
}  // namespace quic
