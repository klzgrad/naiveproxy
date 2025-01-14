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
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_cached_object.h"
#include "quiche/quic/moqt/moqt_failed_fetch.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"

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
  explicit MoqtLiveRelayQueue(FullTrackName track,
                              MoqtForwardingPreference forwarding_preference)
      : track_(std::move(track)),
        forwarding_preference_(forwarding_preference),
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
  bool AddObject(FullSequence sequence, MoqtObjectStatus status) {
    return AddRawObject(sequence, status, publisher_priority_, "");
  }
  bool AddObject(FullSequence sequence, absl::string_view object) {
    return AddRawObject(sequence, MoqtObjectStatus::kNormal,
                        publisher_priority_, object);
  }

  // MoqtTrackPublisher implementation.
  const FullTrackName& GetTrackName() const override { return track_; }
  std::optional<PublishedObject> GetCachedObject(
      FullSequence sequence) const override;
  std::vector<FullSequence> GetCachedObjectsInRange(
      FullSequence start, FullSequence end) const override;
  void AddObjectListener(MoqtObjectListener* listener) override {
    listeners_.insert(listener);
  }
  void RemoveObjectListener(MoqtObjectListener* listener) override {
    listeners_.erase(listener);
  }
  absl::StatusOr<MoqtTrackStatusCode> GetTrackStatus() const override;
  FullSequence GetLargestSequence() const override;
  MoqtForwardingPreference GetForwardingPreference() const override {
    return forwarding_preference_;
  }
  MoqtPriority GetPublisherPriority() const override {
    return publisher_priority_;
  }
  MoqtDeliveryOrder GetDeliveryOrder() const override {
    return delivery_order_;
  }
  std::unique_ptr<MoqtFetchTask> Fetch(FullSequence /*start*/,
                                       uint64_t /*end_group*/,
                                       std::optional<uint64_t> /*end_object*/,
                                       MoqtDeliveryOrder /*order*/) override {
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

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  // Ordered by object id.
  using Subgroup = absl::btree_map<uint64_t, CachedObject>;

  struct Group {
    uint64_t next_object = 0;
    bool complete = false;
    absl::btree_map<SubgroupPriority, Subgroup> subgroups;
  };

  bool AddRawObject(FullSequence sequence, MoqtObjectStatus status,
                    MoqtPriority priority, absl::string_view payload);

  FullTrackName track_;
  MoqtForwardingPreference forwarding_preference_;
  MoqtPriority publisher_priority_ = 128;
  MoqtDeliveryOrder delivery_order_ = MoqtDeliveryOrder::kAscending;
  absl::btree_map<uint64_t, Group> queue_;  // Ordered by group id.
  absl::flat_hash_set<MoqtObjectListener*> listeners_;
  std::optional<FullSequence> end_of_track_;
  FullSequence next_sequence_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_QUEUE_H_
