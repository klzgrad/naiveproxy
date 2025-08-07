// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/test_tools/mock_moqt_session.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_failed_fetch.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt::test {

namespace {
using ::testing::_;
}

// Object listener that forwards all of the objects to the
// SubcribeRemoteTrack::Visitor provided.
class MockMoqtSession::LoopbackObjectListener : public MoqtObjectListener {
 public:
  LoopbackObjectListener(FullTrackName name,
                         SubscribeRemoteTrack::Visitor* visitor,
                         std::shared_ptr<MoqtTrackPublisher> publisher,
                         SubscribeWindow window)
      : name_(name),
        visitor_(visitor),
        publisher_(std::move(publisher)),
        window_(std::move(window)) {
    publisher_->AddObjectListener(this);
  }
  ~LoopbackObjectListener() { publisher_->RemoveObjectListener(this); }

  LoopbackObjectListener(const LoopbackObjectListener&) = delete;
  LoopbackObjectListener(LoopbackObjectListener&&) = delete;
  LoopbackObjectListener& operator=(const LoopbackObjectListener&) = delete;
  LoopbackObjectListener& operator=(LoopbackObjectListener&&) = delete;

  void OnSubscribeAccepted() override {
    visitor_->OnReply(name_,
                      HasObjects()
                          ? std::make_optional(publisher_->GetLargestLocation())
                          : std::nullopt,
                      std::nullopt);
  }

  void OnSubscribeRejected(MoqtSubscribeErrorReason reason,
                           std::optional<uint64_t> track_alias) {
    visitor_->OnReply(name_, std::nullopt, reason.reason_phrase);
  }

  void OnNewObjectAvailable(Location sequence) {
    std::optional<PublishedObject> object =
        publisher_->GetCachedObject(sequence);
    if (!object.has_value()) {
      QUICHE_LOG(FATAL)
          << "GetCachedObject() returned nullopt for a sequence passed into "
             "OnNewObjectAvailable()";
      return;
    }
    if (!window_.InWindow(object->sequence)) {
      return;
    }
    visitor_->OnObjectFragment(name_, sequence, object->publisher_priority,
                               object->status, object->payload.AsStringView(),
                               /*end_of_message=*/true);
  }

  void OnNewFinAvailable(Location sequence) override {}
  void OnSubgroupAbandoned(Location sequence,
                           webtransport::StreamErrorCode error_code) override {}
  void OnGroupAbandoned(uint64_t group_id) override {}
  void OnTrackPublisherGone() override { visitor_->OnSubscribeDone(name_); }

 private:
  bool HasObjects() {
    absl::StatusOr<MoqtTrackStatusCode> status = publisher_->GetTrackStatus();
    if (!status.ok()) {
      return false;
    }
    return *status == MoqtTrackStatusCode::kInProgress ||
           *status == MoqtTrackStatusCode::kFinished;
  }

  FullTrackName name_;
  SubscribeRemoteTrack::Visitor* visitor_;
  std::shared_ptr<MoqtTrackPublisher> publisher_;
  SubscribeWindow window_;
};

bool MockMoqtSession::Subscribe(const FullTrackName& name,
                                SubscribeRemoteTrack::Visitor* visitor,
                                SubscribeWindow window) {
  auto track_publisher = publisher_->GetTrack(name);
  if (!track_publisher.ok()) {
    visitor->OnReply(name, std::nullopt, track_publisher.status().ToString());
    return false;
  }
  auto [it, inserted] = receiving_subscriptions_.insert(
      {name,
       std::make_unique<LoopbackObjectListener>(
           name, visitor, *std::move(track_publisher), std::move(window))});
  return inserted;
}

MockMoqtSession::MockMoqtSession(MoqtPublisher* publisher)
    : publisher_(publisher) {
  ON_CALL(*this, Error)
      .WillByDefault([](MoqtError code, absl::string_view error) {
        ADD_FAILURE() << "Unhandled MoQT fatal error, with code "
                      << static_cast<int>(code) << " and message: " << error;
      });
  if (publisher_ != nullptr) {
    ON_CALL(*this, SubscribeCurrentObject)
        .WillByDefault([this](const FullTrackName& name,
                              SubscribeRemoteTrack::Visitor* visitor,
                              VersionSpecificParameters) {
          return Subscribe(name, visitor, SubscribeWindow());
        });
    ON_CALL(*this, SubscribeAbsolute(_, _, _, _, _))
        .WillByDefault([this](const FullTrackName& name, uint64_t start_group,
                              uint64_t start_object,
                              SubscribeRemoteTrack::Visitor* visitor,
                              VersionSpecificParameters) {
          return Subscribe(
              name, visitor,
              SubscribeWindow(Location(start_group, start_object)));
        });
    ON_CALL(*this, SubscribeAbsolute(_, _, _, _, _, _))
        .WillByDefault([this](const FullTrackName& name, uint64_t start_group,
                              uint64_t start_object, uint64_t end_group,
                              SubscribeRemoteTrack::Visitor* visitor,
                              VersionSpecificParameters) {
          return Subscribe(
              name, visitor,
              SubscribeWindow(Location(start_group, start_object), end_group));
        });
    ON_CALL(*this, Unsubscribe)
        .WillByDefault([this](const FullTrackName& name) {
          receiving_subscriptions_.erase(name);
        });
    ON_CALL(*this, Fetch)
        .WillByDefault(
            [this](const FullTrackName& name, FetchResponseCallback callback,
                   Location start, uint64_t end_group,
                   std::optional<uint64_t> end_object, MoqtPriority priority,
                   std::optional<MoqtDeliveryOrder> delivery_order,
                   VersionSpecificParameters parameters) {
              auto track_publisher = publisher_->GetTrack(name);
              if (!track_publisher.ok()) {
                std::move(callback)(std::make_unique<MoqtFailedFetch>(
                    track_publisher.status()));
                return true;
              }
              std::move(callback)(track_publisher->get()->Fetch(
                  start, end_group, end_object,
                  delivery_order.value_or(MoqtDeliveryOrder::kAscending)));
              return true;
            });
    ON_CALL(*this, JoiningFetch(_, _, _, _))
        .WillByDefault([this](const FullTrackName& name,
                              SubscribeRemoteTrack::Visitor* visitor,
                              uint64_t num_previous_groups,
                              VersionSpecificParameters parameters) {
          return JoiningFetch(
              name, visitor,
              [name, visitor](std::unique_ptr<MoqtFetchTask> fetch) {
                PublishedObject object;
                while (fetch->GetNextObject(object) ==
                       MoqtFetchTask::kSuccess) {
                  visitor->OnObjectFragment(
                      name, object.sequence, object.publisher_priority,
                      object.status, object.payload.AsStringView(), true);
                }
              },
              num_previous_groups, 0x80, std::nullopt, parameters);
        });
    ON_CALL(*this, JoiningFetch(_, _, _, _, _, _, _))
        .WillByDefault([this](const FullTrackName& name,
                              SubscribeRemoteTrack::Visitor* visitor,
                              FetchResponseCallback callback,
                              uint64_t num_previous_groups,
                              MoqtPriority priority,
                              std::optional<MoqtDeliveryOrder> delivery_order,
                              VersionSpecificParameters parameters) {
          SubscribeCurrentObject(name, visitor, parameters);
          auto track_publisher = publisher_->GetTrack(name);
          if (!track_publisher.ok()) {
            std::move(callback)(
                std::make_unique<MoqtFailedFetch>(track_publisher.status()));
            return true;
          }
          if (track_publisher->get()->GetTrackStatus().value_or(
                  MoqtTrackStatusCode::kStatusNotAvailable) ==
              MoqtTrackStatusCode::kNotYetBegun) {
            return Fetch(name, std::move(callback), Location(0, 0), 0, 0,
                         priority, delivery_order, std::move(parameters));
          }
          Location largest = track_publisher->get()->GetLargestLocation();
          uint64_t start_group = largest.group >= num_previous_groups
                                     ? largest.group - num_previous_groups + 1
                                     : 0;
          return Fetch(name, std::move(callback), Location(start_group, 0),
                       largest.group, largest.object, priority, delivery_order,
                       std::move(parameters));
        });
  }
}

MockMoqtSession::~MockMoqtSession() = default;

}  // namespace moqt::test
