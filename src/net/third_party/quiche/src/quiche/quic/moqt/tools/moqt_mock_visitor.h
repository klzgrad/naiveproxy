// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace moqt::test {

struct MockSessionCallbacks {
  testing::MockFunction<void()> session_established_callback;
  testing::MockFunction<void(absl::string_view)> goaway_received_callback;
  testing::MockFunction<void(absl::string_view)> session_terminated_callback;
  testing::MockFunction<void()> session_deleted_callback;
  testing::MockFunction<std::optional<MoqtAnnounceErrorReason>(
      const FullTrackName&, AnnounceEvent)>
      incoming_announce_callback;
  testing::MockFunction<std::optional<MoqtSubscribeErrorReason>(FullTrackName,
                                                                SubscribeEvent)>
      incoming_subscribe_announces_callback;

  MockSessionCallbacks() {
    ON_CALL(incoming_announce_callback, Call(testing::_, testing::_))
        .WillByDefault(DefaultIncomingAnnounceCallback);
    ON_CALL(incoming_subscribe_announces_callback, Call(testing::_, testing::_))
        .WillByDefault(DefaultIncomingSubscribeAnnouncesCallback);
  }

  MoqtSessionCallbacks AsSessionCallbacks() {
    return MoqtSessionCallbacks{
        session_established_callback.AsStdFunction(),
        goaway_received_callback.AsStdFunction(),
        session_terminated_callback.AsStdFunction(),
        session_deleted_callback.AsStdFunction(),
        incoming_announce_callback.AsStdFunction(),
        incoming_subscribe_announces_callback.AsStdFunction()};
  }
};

class MockTrackPublisher : public MoqtTrackPublisher {
 public:
  explicit MockTrackPublisher(FullTrackName name)
      : track_name_(std::move(name)) {
    ON_CALL(*this, GetDeliveryOrder())
        .WillByDefault(testing::Return(MoqtDeliveryOrder::kAscending));
  }
  const FullTrackName& GetTrackName() const override { return track_name_; }

  MOCK_METHOD(std::optional<PublishedObject>, GetCachedObject,
              (FullSequence sequence), (const, override));
  MOCK_METHOD(std::vector<FullSequence>, GetCachedObjectsInRange,
              (FullSequence start, FullSequence end), (const, override));
  MOCK_METHOD(void, AddObjectListener, (MoqtObjectListener * listener),
              (override));
  MOCK_METHOD(void, RemoveObjectListener, (MoqtObjectListener * listener),
              (override));
  MOCK_METHOD(absl::StatusOr<MoqtTrackStatusCode>, GetTrackStatus, (),
              (const, override));
  MOCK_METHOD(FullSequence, GetLargestSequence, (), (const, override));
  MOCK_METHOD(MoqtForwardingPreference, GetForwardingPreference, (),
              (const, override));
  MOCK_METHOD(MoqtPriority, GetPublisherPriority, (), (const, override));
  MOCK_METHOD(MoqtDeliveryOrder, GetDeliveryOrder, (), (const, override));
  MOCK_METHOD(std::unique_ptr<MoqtFetchTask>, Fetch,
              (FullSequence, uint64_t, std::optional<uint64_t>,
               MoqtDeliveryOrder),
              (override));

 private:
  FullTrackName track_name_;
};

class MockSubscribeRemoteTrackVisitor : public SubscribeRemoteTrack::Visitor {
 public:
  MOCK_METHOD(void, OnReply,
              (const FullTrackName& full_track_name,
               std::optional<FullSequence> largest_id,
               std::optional<absl::string_view> error_reason_phrase),
              (override));
  MOCK_METHOD(void, OnCanAckObjects, (MoqtObjectAckFunction ack_function),
              (override));
  MOCK_METHOD(void, OnObjectFragment,
              (const FullTrackName& full_track_name, FullSequence sequence,
               MoqtPriority publisher_priority, MoqtObjectStatus status,
               absl::string_view object, bool end_of_message),
              (override));
};

class MockPublishingMonitorInterface : public MoqtPublishingMonitorInterface {
 public:
  MOCK_METHOD(void, OnObjectAckSupportKnown, (bool supported), (override));
  MOCK_METHOD(void, OnObjectAckReceived,
              (uint64_t group_id, uint64_t object_id,
               quic::QuicTimeDelta delta_from_deadline),
              (override));
};

class MockFetchTask : public MoqtFetchTask {
 public:
  MOCK_METHOD(MoqtFetchTask::GetNextObjectResult, GetNextObject,
              (PublishedObject & output), (override));
  MOCK_METHOD(absl::Status, GetStatus, (), (override));
  MOCK_METHOD(FullSequence, GetLargestId, (), (const, override));

  void SetObjectAvailableCallback(ObjectsAvailableCallback callback) override {
    objects_available_callback_ = std::move(callback);
  }
  ObjectsAvailableCallback& objects_available_callback() {
    return objects_available_callback_;
  };

 private:
  ObjectsAvailableCallback objects_available_callback_;
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_
