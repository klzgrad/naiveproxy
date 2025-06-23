// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SESSION_PEER_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SESSION_PEER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>


#include "absl/status/status.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moqt_mock_visitor.h"
#include "quiche/web_transport/test_tools/mock_web_transport.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt::test {

class MoqtDataParserPeer {
 public:
  static void SetType(MoqtDataParser* parser, MoqtDataStreamType type) {
    parser->type_ = type;
  }
};

class MoqtSessionPeer {
 public:
  static constexpr webtransport::StreamId kControlStreamId = 4;

  static std::unique_ptr<MoqtControlParserVisitor> CreateControlStream(
      MoqtSession* session, webtransport::test::MockStream* stream) {
    auto new_stream =
        std::make_unique<MoqtSession::ControlStream>(session, stream);
    session->control_stream_ = kControlStreamId;
    ON_CALL(*stream, visitor())
        .WillByDefault(::testing::Return(new_stream.get()));
    webtransport::test::MockSession* mock_session =
        static_cast<webtransport::test::MockSession*>(session->session());
    EXPECT_CALL(*mock_session, GetStreamById(kControlStreamId))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(stream));
    return new_stream;
  }

  static std::unique_ptr<MoqtDataParserVisitor> CreateIncomingDataStream(
      MoqtSession* session, webtransport::Stream* stream,
      MoqtDataStreamType type) {
    auto new_stream =
        std::make_unique<MoqtSession::IncomingDataStream>(session, stream);
    MoqtDataParserPeer::SetType(&new_stream->parser_, type);
    return new_stream;
  }

  static std::unique_ptr<webtransport::StreamVisitor>
  CreateIncomingStreamVisitor(MoqtSession* session,
                              webtransport::Stream* stream) {
    auto new_stream =
        std::make_unique<MoqtSession::IncomingDataStream>(session, stream);
    return new_stream;
  }

  // In the test OnSessionReady, the session creates a stream and then passes
  // its unique_ptr to the mock webtransport stream. This function casts
  // that unique_ptr into a MoqtSession::Stream*, which is a private class of
  // MoqtSession, and then casts again into MoqtParserVisitor so that the test
  // can inject packets into that stream.
  // This function is useful for any test that wants to inject packets on a
  // stream created by the MoqtSession.
  static MoqtControlParserVisitor*
  FetchParserVisitorFromWebtransportStreamVisitor(
      MoqtSession* session, webtransport::StreamVisitor* visitor) {
    return static_cast<MoqtSession::ControlStream*>(visitor);
  }

  static void CreateRemoteTrack(MoqtSession* session,
                                const MoqtSubscribe& subscribe,
                                SubscribeRemoteTrack::Visitor* visitor) {
    auto track = std::make_unique<SubscribeRemoteTrack>(subscribe, visitor);
    session->subscribe_by_alias_.try_emplace(subscribe.track_alias,
                                             track.get());
    session->subscribe_by_name_.try_emplace(subscribe.full_track_name,
                                            track.get());
    session->upstream_by_id_.try_emplace(subscribe.request_id,
                                         std::move(track));
  }

  static MoqtObjectListener* AddSubscription(
      MoqtSession* session, std::shared_ptr<MoqtTrackPublisher> publisher,
      uint64_t subscribe_id, uint64_t track_alias, uint64_t start_group,
      uint64_t start_object) {
    MoqtSubscribe subscribe;
    subscribe.full_track_name = publisher->GetTrackName();
    subscribe.track_alias = track_alias;
    subscribe.request_id = subscribe_id;
    subscribe.forward = true;
    subscribe.filter_type = MoqtFilterType::kAbsoluteStart;
    subscribe.start = Location(start_group, start_object);
    subscribe.subscriber_priority = 0x80;
    session->published_subscriptions_.emplace(
        subscribe_id, std::make_unique<MoqtSession::PublishedSubscription>(
                          session, std::move(publisher), subscribe,
                          /*monitoring_interface=*/nullptr));
    return session->published_subscriptions_[subscribe_id].get();
  }

  static bool InSubscriptionWindow(MoqtObjectListener* subscription,
                                   Location sequence) {
    return static_cast<MoqtSession::PublishedSubscription*>(subscription)
        ->InWindow(sequence);
  }

  static MoqtObjectListener* GetSubscription(MoqtSession* session,
                                             uint64_t subscribe_id) {
    auto it = session->published_subscriptions_.find(subscribe_id);
    if (it == session->published_subscriptions_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  static void DeleteSubscription(MoqtSession* session, uint64_t subscribe_id) {
    session->published_subscriptions_.erase(subscribe_id);
  }

  static void UpdateSubscriberPriority(MoqtSession* session,
                                       uint64_t subscribe_id,
                                       MoqtPriority priority) {
    session->published_subscriptions_[subscribe_id]->set_subscriber_priority(
        priority);
  }

  static SubscribeRemoteTrack* remote_track(MoqtSession* session,
                                            uint64_t track_alias) {
    return session->RemoteTrackByAlias(track_alias);
  }

  static void set_next_request_id(MoqtSession* session, uint64_t id) {
    session->next_request_id_ = id;
  }

  static void set_next_incoming_request_id(MoqtSession* session, uint64_t id) {
    session->next_incoming_request_id_ = id;
  }

  static void set_peer_max_request_id(MoqtSession* session, uint64_t id) {
    session->peer_max_request_id_ = id;
  }

  static MoqtSession::PublishedFetch* GetFetch(MoqtSession* session,
                                               uint64_t fetch_id) {
    auto it = session->incoming_fetches_.find(fetch_id);
    if (it == session->incoming_fetches_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  static void ValidateRequestId(MoqtSession* session, uint64_t id) {
    session->ValidateRequestId(id);
  }

  static Location LargestSentForSubscription(MoqtSession* session,
                                             uint64_t subscribe_id) {
    return *session->published_subscriptions_[subscribe_id]->largest_sent();
  }

  // Adds an upstream fetch and a stream ready to receive data.
  static std::unique_ptr<MoqtFetchTask> CreateUpstreamFetch(
      MoqtSession* session, webtransport::Stream* stream) {
    MoqtFetch fetch_message = {
        0,
        128,
        std::nullopt,
        std::nullopt,
        FullTrackName{"foo", "bar"},
        Location{0, 0},
        4,
        std::nullopt,
        VersionSpecificParameters(),
    };
    std::unique_ptr<MoqtFetchTask> task;
    auto [it, success] = session->upstream_by_id_.try_emplace(
        0, std::make_unique<UpstreamFetch>(
               fetch_message, [&](std::unique_ptr<MoqtFetchTask> fetch_task) {
                 task = std::move(fetch_task);
               }));
    QUICHE_DCHECK(success);
    UpstreamFetch* fetch = static_cast<UpstreamFetch*>(it->second.get());
    // Initialize the fetch task
    fetch->OnFetchResult(
        Location{4, 10}, absl::OkStatus(),
        [=, session_ptr = session, fetch_id = fetch_message.fetch_id]() {
          session_ptr->CancelFetch(fetch_id);
        });
    ;
    auto mock_session =
        static_cast<webtransport::test::MockSession*>(session->session());
    EXPECT_CALL(*mock_session, AcceptIncomingUnidirectionalStream())
        .WillOnce(testing::Return(stream))
        .WillOnce(testing::Return(nullptr));
    session->OnIncomingUnidirectionalStreamAvailable();
    return task;
  }

  static quic::QuicAlarmFactory* GetAlarmFactory(MoqtSession* session) {
    return session->alarm_factory_.get();
  }

  static quic::QuicTime Now(MoqtSession* session) {
    return session->callbacks_.clock->ApproximateNow();
  }

  static quic::QuicAlarm* GetAlarm(webtransport::StreamVisitor* visitor) {
    return static_cast<MoqtSession::OutgoingDataStream*>(visitor)
        ->delivery_timeout_alarm_.get();
  }

  static quic::QuicAlarm* GetSubscribeDoneAlarm(
      SubscribeRemoteTrack* subscription) {
    return subscription->subscribe_done_alarm_.get();
  }

  static quic::QuicAlarm* GetGoAwayTimeoutAlarm(MoqtSession* session) {
    return session->goaway_timeout_alarm_.get();
  }

  static quic::QuicTimeDelta GetDeliveryTimeout(
      MoqtObjectListener* subscription) {
    return static_cast<MoqtSession::PublishedSubscription*>(subscription)
        ->delivery_timeout();
  }
  static void SetDeliveryTimeout(MoqtObjectListener* subscription,
                                 quic::QuicTimeDelta timeout) {
    static_cast<MoqtSession::PublishedSubscription*>(subscription)
        ->set_delivery_timeout(timeout);
  }

  static bool SubgroupHasBeenReset(MoqtObjectListener* subscription,
                                   Location sequence) {
    sequence.object = 0;
    return static_cast<MoqtSession::PublishedSubscription*>(subscription)
        ->reset_subgroups()
        .contains(sequence);
  }
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SESSION_PEER_H_
