// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_QUEUE_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

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
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// MoqtLiveRelayQueue lets the user send objects by providing the contents of
// the object and the object metadata. It will store these by sequence number.
// When called on to provide a range of objects, it will fill in any missing
// objects and groups.
//
// The queue will maintain a buffer of three most recent groups that will be
// provided to subscribers automatically.
//
// This class is primarily meant to be used by live relays to buffer the
// frames that arrive for a short time.
class MoqtLiveRelayQueue : public MoqtTrackPublisher {
 public:
  MoqtLiveRelayQueue(
      FullTrackName track,
      std::optional<MoqtForwardingPreference> forwarding_preference,
      std::optional<MoqtDeliveryOrder> delivery_order,
      std::optional<quic::QuicTime> expiration = quic::QuicTime::Infinite(),
      const quic::QuicClock* clock = quic::QuicDefaultClock::Get())
      : clock_(clock),
        track_(std::move(track)),
        forwarding_preference_(forwarding_preference),
        delivery_order_(delivery_order),
        expiration_(expiration),
        next_sequence_(0, 0) {}

  MoqtLiveRelayQueue(const MoqtLiveRelayQueue&) = delete;
  MoqtLiveRelayQueue(MoqtLiveRelayQueue&&) = default;
  MoqtLiveRelayQueue& operator=(const MoqtLiveRelayQueue&) = delete;
  MoqtLiveRelayQueue& operator=(MoqtLiveRelayQueue&&) = default;

  // Publish a received object. Returns false if the object is invalid, given
  // other non-normal objects indicate that the sequence number should not
  // occur. A false return value might result in a session error on the
  // inbound session, but this queue is the only place that retains enough state
  // to check.
  bool AddObject(const PublishedObjectMetadata& metadata,
                 absl::string_view payload, bool fin);

  // Convenience methods primarily for use in tests. Prefer the
  // `PublishedObjectMetadata` version in real forwarding code to ensure all
  // metadata is copied correctly.
  bool AddObject(Location location, uint64_t subgroup, MoqtObjectStatus status,
                 bool fin = false) {
    PublishedObjectMetadata metadata;
    metadata.location = location;
    metadata.subgroup = subgroup;
    metadata.status = status;
    metadata.publisher_priority = 0;
    return AddObject(metadata, "", fin);
  }
  bool AddObject(Location location, uint64_t subgroup, absl::string_view object,
                 bool fin = false) {
    PublishedObjectMetadata metadata;
    metadata.location = location;
    metadata.subgroup = subgroup;
    metadata.status = MoqtObjectStatus::kNormal;
    metadata.publisher_priority = 0;
    return AddObject(metadata, object, fin);
  }

  // Record a received FIN from upstream that did not come with the last object.
  // If the forwarding preference is kDatagram or kTrack, |sequence| is ignored.
  // Otherwise, |sequence| is used to determine which stream is being FINed. If
  // the object ID does not match the last object ID in the stream, no action
  // is taken.
  bool AddFin(Location sequence, uint64_t subgroup_id);
  // Record a received RESET_STREAM from upstream. Returns false on datagram
  // tracks, or if the stream does not exist.
  bool OnStreamReset(uint64_t group_id, uint64_t subgroup_id,
                     webtransport::StreamErrorCode error_code);

  // MoqtTrackPublisher implementation.
  const FullTrackName& GetTrackName() const override { return track_; }
  std::optional<PublishedObject> GetCachedObject(
      uint64_t group_id, uint64_t subgroup_id,
      uint64_t min_object) const override;
  void AddObjectListener(MoqtObjectListener* listener) override {
    listeners_.insert(listener);
    listener->OnSubscribeAccepted();
  }
  void RemoveObjectListener(MoqtObjectListener* listener) override {
    listeners_.erase(listener);
  }
  std::optional<Location> largest_location() const override;
  std::optional<MoqtForwardingPreference> forwarding_preference()
      const override {
    return forwarding_preference_;
  }
  std::optional<MoqtDeliveryOrder> delivery_order() const override {
    return delivery_order_;
  }
  std::optional<quic::QuicTimeDelta> expiration() const override {
    if (!expiration_.has_value()) {
      return std::nullopt;
    }
    if (expiration_ == quic::QuicTime::Infinite()) {
      return quic::QuicTimeDelta::Zero();
    }
    if (expiration_ < clock_->Now()) {
      // TODO(martinduke): Tear everything down; the track is expired.
      return quic::QuicTimeDelta::Zero();
    }
    return clock_->Now() - *expiration_;
  }
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

  bool HasSubscribers() const { return !listeners_.empty(); }

  // Since MoqtTrackPublisher is generally held in a shared_ptr, an explicit
  // call allows all the listeners to delete their reference and actually
  // destroy the object.
  void RemoveAllSubscriptions() {
    for (MoqtObjectListener* listener : listeners_) {
      listener->OnTrackPublisherGone();
    }
  }

  void ForAllObjects(
      quiche::UnretainedCallback<void(const CachedObject&)> callback);

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  // Ordered by object id.
  using Subgroup = absl::btree_map<uint64_t, CachedObject>;

  struct Group {
    uint64_t next_object = 0;
    bool complete = false;  // If true, kEndOfGroup has been received.
    absl::btree_map<uint64_t, Subgroup> subgroups;  // Ordered by subgroup id.
  };

  const quic::QuicClock* clock_;
  FullTrackName track_;
  std::optional<MoqtForwardingPreference> forwarding_preference_;
  std::optional<MoqtDeliveryOrder> delivery_order_;
  std::optional<quic::QuicTime> expiration_;
  absl::btree_map<uint64_t, Group> queue_;  // Ordered by group id.
  absl::flat_hash_set<MoqtObjectListener*> listeners_;
  std::optional<Location> end_of_track_;
  Location next_sequence_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_QUEUE_H_
