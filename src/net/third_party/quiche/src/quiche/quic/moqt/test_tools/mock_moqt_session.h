// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/test_tools/mock_web_transport.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {
namespace test {

class MockSessionToPublisherInterface : public SessionToPublisherInterface {
 public:
  MockSessionToPublisherInterface() : weak_ptr_factory_(this) {}
  ~MockSessionToPublisherInterface() override = default;
  MOCK_METHOD(bool, alternate_delivery_timeout, (), (const, override));
  MOCK_METHOD(void, UpdateTrackPriority,
              (uint64_t, std::optional<MoqtTrackPriority>, MoqtTrackPriority),
              (override));
  MOCK_METHOD(quic::QuicAlarmFactory*, alarm_factory, (), (override));
  MOCK_METHOD(std::shared_ptr<MoqtTrackPublisher>, GetTrackPublisher,
              (const FullTrackName&), (override));
  MOCK_METHOD(MoqtPublishingMonitorInterface*, ReleaseMonitoringInterface,
              (const FullTrackName&), (override));
  MOCK_METHOD(const quic::QuicClock*, clock, (), (override));
  MOCK_METHOD(MoqtTraceRecorder&, trace_recorder, (), (override));
  MOCK_METHOD(webtransport::Session*, session, (), (override));

  quiche::QuicheWeakPtrFactory<SessionToPublisherInterface> weak_ptr_factory_;
};

class MockMoqtSession : public MoqtSessionInterface {
 public:
  MOCK_METHOD(MoqtSessionCallbacks&, callbacks, (), (override));
  MOCK_METHOD(void, Error, (MoqtError code, absl::string_view error),
              (override));
  MOCK_METHOD(bool, Subscribe,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               const MessageParameters& parameters),
              (override));
  MOCK_METHOD(bool, SubscribeUpdate,
              (const FullTrackName&, const MessageParameters&,
               MoqtResponseCallback),
              (override));
  MOCK_METHOD(bool, PublishUpdate,
              (const FullTrackName& name, const MessageParameters& parameters,
               MoqtResponseCallback response_callback),
              (override));
  MOCK_METHOD(void, Unsubscribe, (const FullTrackName& name), (override));
  MOCK_METHOD(bool, Publish,
              (std::shared_ptr<MoqtTrackPublisher> publisher,
               const MessageParameters& parameters,
               const TrackExtensions& extensions,
               MoqtResponseCallback response_callback),
              (override));
  MOCK_METHOD(bool, Fetch,
              (const FullTrackName& name, FetchResponseCallback callback,
               Location start, uint64_t end_group,
               std::optional<uint64_t> end_object,
               MessageParameters parameters),
              (override));
  MOCK_METHOD(bool, RelativeJoiningFetch,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               uint64_t num_previous_groups, MessageParameters parameters),
              (override));
  MOCK_METHOD(bool, RelativeJoiningFetch,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               FetchResponseCallback callback, uint64_t num_previous_groups,
               MessageParameters parameters),
              (override));
  MOCK_METHOD(bool, PublishNamespace,
              (const TrackNamespace& track_namespace,
               const MessageParameters& parameters,
               MoqtResponseCallback response_callback,
               quiche::SingleUseCallback<void()> cancel_callback),
              (override));
  MOCK_METHOD(bool, PublishNamespaceUpdate,
              (const TrackNamespace& track_namespace,
               MessageParameters& parameters,
               MoqtResponseCallback response_callback),
              (override));
  MOCK_METHOD(bool, PublishNamespaceDone,
              (const TrackNamespace& track_namespace), (override));
  MOCK_METHOD(bool, PublishNamespaceCancel,
              (const TrackNamespace& track_namespace,
               webtransport::StreamErrorCode error_code),
              (override));
  MOCK_METHOD(std::unique_ptr<MoqtNamespaceTask>, SubscribeNamespace,
              (TrackNamespace&, const MessageParameters&, MoqtResponseCallback),
              (override));
  MOCK_METHOD(bool, SubscribeTracks,
              (TrackNamespace&, const MessageParameters&, MoqtResponseCallback),
              (override));
  MOCK_METHOD(void, UnsubscribeTracks, (TrackNamespace&), (override));
  MOCK_METHOD(bool, TrackStatus,
              (const FullTrackName&, const MessageParameters&,
               MoqtResponseCallback),
              (override));

  quiche::QuicheWeakPtr<MoqtSessionInterface> GetWeakPtr() override {
    return weak_factory_.Create();
  }
  quiche::QuicheWeakPtrFactory<MoqtSessionInterface> weak_factory_{this};
};

inline void ExpectFin(webtransport::test::MockStream& stream,
                      bool has_data = false) {
  EXPECT_CALL(stream, Writev)
      .WillOnce([has_data](absl::Span<quiche::QuicheMemSlice> data,
                           const webtransport::StreamWriteOptions& options) {
        EXPECT_EQ(data.empty(), !has_data);
        EXPECT_TRUE(options.send_fin());
        return absl::OkStatus();
      });
}

class MockBidiStream : public MoqtBidiStreamBase {
 public:
  MockBidiStream()
      : MoqtBidiStreamBase(reinterpret_cast<MoqtFramer*>(0x1),
                           MoqtControlMessageParser(
                               "moqt-00", true, quic::Perspective::IS_CLIENT),
                           nullptr) {}
  MockBidiStream(MoqtFramer* absl_nonnull framer,
                 const MoqtControlMessageParser& message_parser,
                 SessionErrorCallback session_error_callback)
      : MoqtBidiStreamBase(framer, message_parser,
                           std::move(session_error_callback)) {}

  MOCK_METHOD(absl::Status, SendRequestUpdate,
              (uint64_t request_id, uint64_t existing_request_id,
               const MessageParameters& parameters,
               MoqtResponseCallback callback),
              (override));
  MOCK_METHOD(void, Detach, (), (override));
  MOCK_METHOD(void, OnStreamBound, (), (override));
  MOCK_METHOD(absl::Status, OnRawControlMessage,
              (const MoqtRawControlMessage& message), (override));
};

}  // namespace test
}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_
