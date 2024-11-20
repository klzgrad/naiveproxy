// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_cached_object.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

// MoqtOutgoingQueue lets the user send objects by providing the contents of the
// object and a keyframe flag.  The queue will automatically number objects and
// groups, and maintain a buffer of three most recent groups that will be
// provided to subscribers automatically.
//
// This class is primarily meant to be used by original publishers to buffer the
// frames that they produce.
class MoqtOutgoingQueue : public MoqtTrackPublisher {
 public:
  explicit MoqtOutgoingQueue(FullTrackName track,
                             MoqtForwardingPreference forwarding_preference)
      : track_(std::move(track)),
        forwarding_preference_(forwarding_preference) {}

  MoqtOutgoingQueue(const MoqtOutgoingQueue&) = delete;
  MoqtOutgoingQueue(MoqtOutgoingQueue&&) = default;
  MoqtOutgoingQueue& operator=(const MoqtOutgoingQueue&) = delete;
  MoqtOutgoingQueue& operator=(MoqtOutgoingQueue&&) = default;

  // If `key` is true, the object is placed into a new group, and the previous
  // group is closed. The first object ever sent MUST have `key` set to true.
  void AddObject(quiche::QuicheMemSlice payload, bool key);

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

  bool HasSubscribers() const { return !listeners_.empty(); }
  void SetDeliveryOrder(MoqtDeliveryOrder order) {
    // TODO: add test coverage.
    delivery_order_ = order;
  }

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  using Group = std::vector<CachedObject>;

  void AddRawObject(MoqtObjectStatus status, quiche::QuicheMemSlice payload);

  // The number of the oldest group available.
  uint64_t first_group_in_queue() const {
    return current_group_id_ - queue_.size() + 1;
  }

  FullTrackName track_;
  MoqtForwardingPreference forwarding_preference_;
  MoqtPriority publisher_priority_ = 128;
  MoqtDeliveryOrder delivery_order_ = MoqtDeliveryOrder::kAscending;
  absl::InlinedVector<Group, kMaxQueuedGroups> queue_;
  uint64_t current_group_id_ = -1;
  absl::flat_hash_set<MoqtObjectListener*> listeners_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_
