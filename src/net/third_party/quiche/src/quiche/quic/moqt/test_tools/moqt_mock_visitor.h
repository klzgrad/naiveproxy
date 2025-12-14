// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt::test {

struct MockSessionCallbacks {
  testing::MockFunction<void()> session_established_callback;
  testing::MockFunction<void(absl::string_view)> goaway_received_callback;
  testing::MockFunction<void(absl::string_view)> session_terminated_callback;
  testing::MockFunction<void()> session_deleted_callback;
  testing::MockFunction<void(const TrackNamespace&,
                             std::optional<VersionSpecificParameters>,
                             MoqtResponseCallback)>
      incoming_publish_namespace_callback;
  testing::MockFunction<void(const TrackNamespace&,
                             std::optional<VersionSpecificParameters>,
                             MoqtResponseCallback)>
      incoming_subscribe_namespace_callback;

  MockSessionCallbacks() {
    ON_CALL(incoming_publish_namespace_callback, Call)
        .WillByDefault(DefaultIncomingPublishNamespaceCallback);
    ON_CALL(incoming_subscribe_namespace_callback, Call)
        .WillByDefault(DefaultIncomingSubscribeNamespaceCallback);
  }

  MoqtSessionCallbacks AsSessionCallbacks() {
    return MoqtSessionCallbacks{
        session_established_callback.AsStdFunction(),
        goaway_received_callback.AsStdFunction(),
        session_terminated_callback.AsStdFunction(),
        session_deleted_callback.AsStdFunction(),
        incoming_publish_namespace_callback.AsStdFunction(),
        incoming_subscribe_namespace_callback.AsStdFunction()};
  }
};

class MockTrackPublisher : public MoqtTrackPublisher {
 public:
  explicit MockTrackPublisher(FullTrackName name)
      : track_name_(std::move(name)) {
    ON_CALL(*this, delivery_order())
        .WillByDefault(testing::Return(MoqtDeliveryOrder::kAscending));
  }
  const FullTrackName& GetTrackName() const override { return track_name_; }

  MOCK_METHOD(std::optional<PublishedObject>, GetCachedObject,
              (uint64_t, uint64_t, uint64_t), (const, override));
  MOCK_METHOD(void, AddObjectListener, (MoqtObjectListener * listener),
              (override));
  MOCK_METHOD(void, RemoveObjectListener, (MoqtObjectListener * listener),
              (override));
  MOCK_METHOD(std::optional<Location>, largest_location, (), (const, override));
  MOCK_METHOD(std::optional<MoqtForwardingPreference>, forwarding_preference,
              (), (const, override));
  MOCK_METHOD(std::optional<MoqtDeliveryOrder>, delivery_order, (),
              (const, override));
  MOCK_METHOD(std::optional<quic::QuicTimeDelta>, expiration, (),
              (const, override));
  MOCK_METHOD(std::unique_ptr<MoqtFetchTask>, StandaloneFetch,
              (Location, Location, std::optional<MoqtDeliveryOrder>),
              (override));
  MOCK_METHOD(std::unique_ptr<MoqtFetchTask>, RelativeFetch,
              (uint64_t, std::optional<MoqtDeliveryOrder>), (override));
  MOCK_METHOD(std::unique_ptr<MoqtFetchTask>, AbsoluteFetch,
              (uint64_t, std::optional<MoqtDeliveryOrder>), (override));

 private:
  FullTrackName track_name_;
};

// TODO(martinduke): Rename to MockSubscribeVisitor.
class MockSubscribeRemoteTrackVisitor : public SubscribeVisitor {
 public:
  MOCK_METHOD(void, OnReply,
              (const FullTrackName& full_track_name,
               (std::variant<SubscribeOkData, MoqtRequestError> response)),
              (override));
  MOCK_METHOD(void, OnCanAckObjects, (MoqtObjectAckFunction ack_function),
              (override));
  MOCK_METHOD(void, OnObjectFragment,
              (const FullTrackName& full_track_name,
               const PublishedObjectMetadata& metadata,
               absl::string_view object, bool end_of_message),
              (override));
  MOCK_METHOD(void, OnPublishDone, (FullTrackName full_track_name), (override));
  MOCK_METHOD(void, OnMalformedTrack, (const FullTrackName& full_track_name),
              (override));
  MOCK_METHOD(void, OnStreamFin,
              (const FullTrackName& full_track_name, DataStreamIndex stream),
              (override));
  MOCK_METHOD(void, OnStreamReset,
              (const FullTrackName& full_track_name, DataStreamIndex stream),
              (override));
};

class MockPublishingMonitorInterface : public MoqtPublishingMonitorInterface {
 public:
  MOCK_METHOD(void, OnObjectAckSupportKnown,
              (std::optional<quic::QuicTimeDelta> time_window), (override));
  MOCK_METHOD(void, OnObjectAckReceived,
              (uint64_t group_id, uint64_t object_id,
               quic::QuicTimeDelta delta_from_deadline),
              (override));
};

class MockFetchTask : public MoqtFetchTask {
 public:
  MockFetchTask() {};  // No synchronous callbacks.
  MockFetchTask(std::optional<MoqtFetchOk> fetch_ok,
                std::optional<MoqtFetchError> fetch_error,
                bool synchronous_object_available)
      : synchronous_fetch_ok_(fetch_ok),
        synchronous_fetch_error_(fetch_error),
        synchronous_object_available_(synchronous_object_available) {
    QUICHE_DCHECK(!synchronous_fetch_ok_.has_value() ||
                  !synchronous_fetch_error_.has_value());
  }

  MOCK_METHOD(MoqtFetchTask::GetNextObjectResult, GetNextObject,
              (PublishedObject & output), (override));
  MOCK_METHOD(absl::Status, GetStatus, (), (override));

  void SetObjectAvailableCallback(ObjectsAvailableCallback callback) override {
    objects_available_callback_ = std::move(callback);
    if (synchronous_object_available_) {
      // The first call is installed by the session to trigger stream creation.
      // An object might not exist yet.
      objects_available_callback_();
    }
    // The second call is a result of the stream replacing the callback, which
    // means there is an object available.
    synchronous_object_available_ = true;
  }
  void SetFetchResponseCallback(FetchResponseCallback callback) override {
    if (synchronous_fetch_ok_.has_value()) {
      std::move(callback)(*synchronous_fetch_ok_);
      return;
    }
    if (synchronous_fetch_error_.has_value()) {
      std::move(callback)(*synchronous_fetch_error_);
      return;
    }
    fetch_response_callback_ = std::move(callback);
  }

  void CallObjectsAvailableCallback() { objects_available_callback_(); };
  void CallFetchResponseCallback(
      std::variant<MoqtFetchOk, MoqtFetchError> response) {
    std::move(fetch_response_callback_)(response);
  }

 private:
  FetchResponseCallback fetch_response_callback_;
  ObjectsAvailableCallback objects_available_callback_;
  std::optional<MoqtFetchOk> synchronous_fetch_ok_;
  std::optional<MoqtFetchError> synchronous_fetch_error_;
  bool synchronous_object_available_ = false;
};

class MockMoqtObjectListener : public MoqtObjectListener {
 public:
  MOCK_METHOD(void, OnSubscribeAccepted, (), (override));
  MOCK_METHOD(void, OnSubscribeRejected, (MoqtRequestError), (override));
  MOCK_METHOD(void, OnNewObjectAvailable, (Location, uint64_t, MoqtPriority),
              (override));
  MOCK_METHOD(void, OnNewFinAvailable, (Location, uint64_t), (override));
  MOCK_METHOD(void, OnSubgroupAbandoned,
              (uint64_t, uint64_t, webtransport::StreamErrorCode), (override));
  MOCK_METHOD(void, OnGroupAbandoned, (uint64_t), (override));
  MOCK_METHOD(void, OnTrackPublisherGone, (), (override));
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_
