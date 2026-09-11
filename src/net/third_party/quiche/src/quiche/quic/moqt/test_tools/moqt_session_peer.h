// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SESSION_PEER_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SESSION_PEER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object_subscriber.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/quic/moqt/moqt_uni_stream.h"
#include "quiche/quic/moqt/test_tools/moqt_framer_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/web_transport/test_tools/mock_web_transport.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt::test {

class MoqtDataParserPeer {
 public:
  static void SetType(MoqtDataParser* parser, MoqtDataStreamType type) {
    parser->type_ = type;
    parser->next_input_ = MoqtDataParser::NextInput::kTrackAlias;
  }
  static void SetTrackAlias(MoqtDataParser* parser, uint64_t track_alias) {
    parser->metadata_.track_alias = track_alias;
    parser->next_input_ = MoqtDataParser::NextInput::kGroupId;
  }
};

// Helper class to interact with MOQT bidi streams in tests.
class MoqtBidiStreamTestWrapper {
 public:
  explicit MoqtBidiStreamTestWrapper(
      std::unique_ptr<MoqtBidiStreamBase> absl_nonnull stream)
      : stream_(std::move(stream)) {}

  MoqtBidiStreamTestWrapper(
      std::unique_ptr<MoqtSession::OutgoingControlStream> absl_nonnull
      outgoing_stream,
      std::unique_ptr<MoqtSession::IncomingControlStream> absl_nonnull
      incoming_stream)
      : outgoing_control_stream_(std::move(outgoing_stream)),
        incoming_control_stream_(std::move(incoming_stream)) {}

  MoqtBidiStreamBase& stream() {
    QUICHE_DCHECK(stream_ != nullptr);
    return *stream_;
  }

  MoqtSession::OutgoingControlStream& outgoing_control_stream() {
    QUICHE_DCHECK(outgoing_control_stream_ != nullptr);
    return *outgoing_control_stream_;
  }

  MoqtSession::IncomingControlStream& incoming_control_stream() {
    QUICHE_DCHECK(incoming_control_stream_ != nullptr);
    return *incoming_control_stream_;
  }

  // Simulates receiving the specified control message on the bidi stream.
  void ReceiveMessage(const AnyMoqtControlMessage& message) {
    std::string serialized = SerializeGenericMessage(message);
    quiche::QuicheDataReader reader(serialized);
    uint64_t raw_type;
    ASSERT_TRUE(reader.ReadMoqVarInt(&raw_type));
    ASSERT_TRUE(reader.Seek(2));
    MoqtRawControlMessage raw_message{
        .type = static_cast<MoqtMessageType>(raw_type),
        .payload = std::string(reader.ReadRemainingPayload())};
    if (stream_ != nullptr) {
      absl::Status status = stream_->OnRawControlMessage(raw_message);
      stream_->CheckStatus(status);
      return;
    }
    QUICHE_DCHECK(incoming_control_stream_ != nullptr);
    MoqtSession* session =
        MoqtSessionFromWeakPtr(incoming_control_stream_->session_);
    QUICHE_DCHECK(session != nullptr);
    absl::Status status = ControlMessageDispatcher::DispatchControlMessage(
        *session, session->ControlMessageParser(), raw_message, "control");
    if (!status.ok()) {
      std::optional<MoqtError> error_code = GetMoqtErrorForStatus(status);
      session->Error(error_code.value_or(MoqtError::kProtocolViolation),
                     status.message());
    }
  }

 private:
  std::unique_ptr<MoqtBidiStreamBase> stream_;
  std::unique_ptr<MoqtSession::OutgoingControlStream> outgoing_control_stream_;
  std::unique_ptr<MoqtSession::IncomingControlStream> incoming_control_stream_;
};

class OutgoingSubgroupStreamPeer {
 public:
  static quic::QuicAlarm* GetAlarm(OutgoingSubgroupStream* stream) {
    return stream->delivery_timeout_alarm_.get();
  }
};

class MoqtSessionPeer {
 public:
  static constexpr webtransport::StreamId kControlStreamId = 4;

  static std::unique_ptr<MoqtBidiStreamTestWrapper> CreateControlStream(
      MoqtSession* session, webtransport::test::MockStream* stream) {
    auto outgoing =
        std::make_unique<MoqtSession::OutgoingControlStream>(session, stream);
    session->outgoing_control_stream_ = outgoing->GetWeakPtr();
    auto incoming = std::make_unique<MoqtSession::IncomingControlStream>(
        session, MoqtStreamTypeParser(stream));
    session->incoming_control_stream_ = incoming->GetWeakPtr();
    ON_CALL(*stream, visitor())
        .WillByDefault(::testing::Return(outgoing.get()));
    ON_CALL(*stream, CanWrite).WillByDefault(::testing::Return(true));
    return std::make_unique<MoqtBidiStreamTestWrapper>(std::move(outgoing),
                                                       std::move(incoming));
  }

  static std::unique_ptr<webtransport::StreamVisitor>
  CreateIncomingStreamVisitor(MoqtSession* session,
                              webtransport::Stream* stream) {
    auto new_stream = std::make_unique<IncomingDataStream>(
        stream, session, session->callbacks_.clock);
    return new_stream;
  }

  static bool RequestIdIsLivePublisher(MoqtSession* session,
                                       uint64_t request_id) {
    return session->published_subscriptions_.contains(request_id);
  }

  static void set_next_request_id(MoqtSession* session, uint64_t id) {
    session->next_request_id_ = id;
  }

  static void set_peer_max_request_id(MoqtSession* session, uint64_t id) {
    session->peer_max_request_id_ = id;
  }

  static void set_peer_setup_received(MoqtSession* session, bool value) {
    session->peer_setup_received_ = value;
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

  static quic::QuicAlarmFactory* GetAlarmFactory(MoqtSession* session) {
    return session->alarm_factory_.get();
  }

  static quic::QuicTime Now(MoqtSession* session) {
    return session->callbacks_.clock->ApproximateNow();
  }

  static quic::QuicAlarm* GetPublishDoneAlarm(LiveSubscriber* subscription) {
    return subscription->publish_done_alarm_.get();
  }

  static quic::QuicAlarm* GetGoAwayTimeoutAlarm(MoqtSession* session) {
    return session->goaway_timeout_alarm_.get();
  }

  static quic::QuicTimeDelta GetDeliveryTimeout(MoqtSession* session,
                                                uint64_t request_id) {
    auto it = session->published_subscriptions_.find(request_id);
    if (it == session->published_subscriptions_.end()) {
      return quic::QuicTimeDelta::Zero();
    }
    return it->second->delivery_timeout();
  }

  static absl::string_view GetImplementationString(MoqtSession* session) {
    return session->parameters_.moqt_implementation;
  }

  static MoqtSession::OutgoingControlStream* GetOutgoingControlStream(
      MoqtSession* session) {
    return session->outgoing_control_stream_.GetIfAvailable();
  }
  static MoqtSession::IncomingControlStream* GetIncomingControlStream(
      MoqtSession* session) {
    return session->incoming_control_stream_.GetIfAvailable();
  }
  static MoqtSession::OutgoingControlStream* GetControlStream(
      MoqtSession* session) {
    return session->outgoing_control_stream_.GetIfAvailable();
  }

  static const MoqtSessionParameters& GetParameters(MoqtSession* session) {
    return session->parameters_;
  }

  static std::optional<uint64_t> NextQueuedRequestIdToServer(
      MoqtSession* session) {
    return session->subscriptions_with_queued_streams_.empty()
               ? std::optional<uint64_t>()
               : session->subscriptions_with_queued_streams_.begin()->second;
  }

  static uint64_t GetLastTrackAlias(MoqtSession* session) {
    return session->next_local_track_alias_ - 1;
  }
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_SESSION_PEER_H_
