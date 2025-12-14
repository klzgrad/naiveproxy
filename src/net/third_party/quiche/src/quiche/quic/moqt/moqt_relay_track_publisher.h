// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_RELAY_TRACK_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_RELAY_TRACK_PUBLISHER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

using DeleteTrackCallback = quiche::SingleUseCallback<void()>;

// MoqtRelayTrackPublisher lets the user send objects by providing the contents
// of the object and the object metadata. It will store these by location
// number. When called on to provide a range of objects, it will fill in any
// missing objects and groups.
//
// The queue will maintain a buffer of three most recent groups that will be
// provided to subscribers automatically.
//
// This class is primarily meant to be used by live relays to buffer the
// frames that arrive for a short time.
class MoqtRelayTrackPublisher : public MoqtTrackPublisher,
                                public SubscribeVisitor {
 public:
  MoqtRelayTrackPublisher(
      FullTrackName track, quiche::QuicheWeakPtr<MoqtSessionInterface> upstream,
      DeleteTrackCallback delete_track_callback,
      std::optional<MoqtForwardingPreference> forwarding_preference,
      std::optional<MoqtDeliveryOrder> delivery_order,
      std::optional<quic::QuicTime> expiration = quic::QuicTime::Infinite(),
      const quic::QuicClock* clock = quic::QuicDefaultClock::Get())
      : clock_(clock),
        track_(std::move(track)),
        upstream_(std::move(upstream)),
        delete_track_callback_(std::move(delete_track_callback)),
        forwarding_preference_(forwarding_preference),
        delivery_order_(delivery_order),
        expiration_(expiration),
        next_location_(0, 0) {}

  MoqtRelayTrackPublisher(const MoqtRelayTrackPublisher&) = delete;
  MoqtRelayTrackPublisher(MoqtRelayTrackPublisher&&) = default;
  MoqtRelayTrackPublisher& operator=(const MoqtRelayTrackPublisher&) = delete;
  MoqtRelayTrackPublisher& operator=(MoqtRelayTrackPublisher&&) = default;

  // SubscribeVisitor implementation.
  void OnReply(
      const FullTrackName& full_track_name,
      std::variant<SubscribeOkData, MoqtRequestError> response) override;
  // TODO(vasilvv): Implement this if we want to support Object Acks across
  // relays.
  void OnCanAckObjects(MoqtObjectAckFunction /*ack_function*/) override {}
  void OnObjectFragment(const FullTrackName& full_track_name,
                        const PublishedObjectMetadata& metadata,
                        absl::string_view object, bool end_of_message) override;
  void OnPublishDone(FullTrackName /*full_track_name*/) override {}
  void OnMalformedTrack(const FullTrackName& /*full_track_name*/) override {
    DeleteTrack();
  }
  void OnStreamFin(const FullTrackName&, DataStreamIndex stream) override;
  void OnStreamReset(const FullTrackName&, DataStreamIndex stream) override;

  // MoqtTrackPublisher implementation.
  const FullTrackName& GetTrackName() const override { return track_; }
  std::optional<PublishedObject> GetCachedObject(
      uint64_t group_id, uint64_t subgroup_id,
      uint64_t min_object) const override;
  void AddObjectListener(MoqtObjectListener* listener) override;
  void RemoveObjectListener(MoqtObjectListener* listener) override;
  std::optional<Location> largest_location() const override;
  std::optional<MoqtForwardingPreference> forwarding_preference()
      const override {
    return forwarding_preference_;
  }
  std::optional<MoqtDeliveryOrder> delivery_order() const override {
    return delivery_order_;
  }
  std::optional<quic::QuicTimeDelta> expiration() const override;
  std::unique_ptr<MoqtFetchTask> StandaloneFetch(
      Location /*start*/, Location /*end*/,
      std::optional<MoqtDeliveryOrder> /*order*/) override {
    return std::make_unique<MoqtFailedFetch>(
        absl::UnimplementedError("Fetch not implemented"));
  }
  std::unique_ptr<MoqtFetchTask> RelativeFetch(
      uint64_t /*group_diff*/,
      std::optional<MoqtDeliveryOrder> /*order*/) override {
    return std::make_unique<MoqtFailedFetch>(
        absl::UnimplementedError("Fetch not implemented"));
  }
  std::unique_ptr<MoqtFetchTask> AbsoluteFetch(
      uint64_t /*group*/, std::optional<MoqtDeliveryOrder> /*order*/) override {
    return std::make_unique<MoqtFailedFetch>(
        absl::UnimplementedError("Fetch not implemented"));
  }

  void ForAllObjects(
      quiche::UnretainedCallback<void(const CachedObject&)> callback);

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  void DeleteTrack();

  // Ordered by object id.
  using Subgroup = absl::btree_map<uint64_t, CachedObject>;

  struct Group {
    uint64_t next_object = 0;
    bool complete = false;  // If true, kEndOfGroup has been received.
    absl::btree_map<uint64_t, Subgroup> subgroups;  // Ordered by subgroup id.
  };

  const quic::QuicClock* clock_;
  FullTrackName track_;
  quiche::QuicheWeakPtr<MoqtSessionInterface> upstream_;
  DeleteTrackCallback delete_track_callback_;
  std::optional<MoqtForwardingPreference> forwarding_preference_;
  std::optional<MoqtDeliveryOrder> delivery_order_;
  // TODO(martinduke): This publisher should destroy itself when the expiration
  // time passes.
  std::optional<quic::QuicTime> expiration_;
  absl::btree_map<uint64_t, Group> queue_;  // Ordered by group id.
  absl::flat_hash_set<MoqtObjectListener*> listeners_;
  std::optional<Location> end_of_track_;
  Location next_location_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_RELAY_TRACK_PUBLISHER_H_
