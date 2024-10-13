// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_outgoing_queue.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_cached_object.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

void MoqtOutgoingQueue::AddObject(quiche::QuicheMemSlice payload, bool key) {
  if (queue_.empty() && !key) {
    QUICHE_BUG(MoqtOutgoingQueue_AddObject_first_object_not_key)
        << "The first object ever added to the queue must have the \"key\" "
           "flag.";
    return;
  }

  if (key) {
    if (!queue_.empty()) {
      AddRawObject(MoqtObjectStatus::kEndOfGroup, quiche::QuicheMemSlice());
    }

    if (queue_.size() == kMaxQueuedGroups) {
      queue_.erase(queue_.begin());
    }
    queue_.emplace_back();
    ++current_group_id_;
  }

  AddRawObject(MoqtObjectStatus::kNormal, std::move(payload));
}

void MoqtOutgoingQueue::AddRawObject(MoqtObjectStatus status,
                                     quiche::QuicheMemSlice payload) {
  FullSequence sequence{current_group_id_, queue_.back().size()};
  queue_.back().push_back(CachedObject{
      sequence, status,
      std::make_shared<quiche::QuicheMemSlice>(std::move(payload))});
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(sequence);
  }
}

std::optional<PublishedObject> MoqtOutgoingQueue::GetCachedObject(
    FullSequence sequence) const {
  if (sequence.group < first_group_in_queue()) {
    return PublishedObject{FullSequence{sequence.group, sequence.object},
                           MoqtObjectStatus::kGroupDoesNotExist,
                           quiche::QuicheMemSlice()};
  }
  if (sequence.group > current_group_id_) {
    return std::nullopt;
  }
  const std::vector<CachedObject>& group =
      queue_[sequence.group - first_group_in_queue()];
  if (sequence.object >= group.size()) {
    if (sequence.group == current_group_id_) {
      return std::nullopt;
    }
    return PublishedObject{FullSequence{sequence.group, sequence.object},
                           MoqtObjectStatus::kObjectDoesNotExist,
                           quiche::QuicheMemSlice()};
  }
  QUICHE_DCHECK(sequence == group[sequence.object].sequence);
  return CachedObjectToPublishedObject(group[sequence.object]);
}

std::vector<FullSequence> MoqtOutgoingQueue::GetCachedObjectsInRange(
    FullSequence start, FullSequence end) const {
  std::vector<FullSequence> sequences;
  SubscribeWindow window(start, end);
  for (const Group& group : queue_) {
    for (const CachedObject& object : group) {
      if (window.InWindow(object.sequence)) {
        sequences.push_back(object.sequence);
      }
    }
  }
  return sequences;
}

absl::StatusOr<MoqtTrackStatusCode> MoqtOutgoingQueue::GetTrackStatus() const {
  if (queue_.empty()) {
    return MoqtTrackStatusCode::kNotYetBegun;
  }
  return MoqtTrackStatusCode::kInProgress;
}

FullSequence MoqtOutgoingQueue::GetLargestSequence() const {
  if (queue_.empty()) {
    QUICHE_BUG(MoqtOutgoingQueue_GetLargestSequence_not_begun)
        << "Calling GetLargestSequence() on a track that hasn't begun";
    return FullSequence{0, 0};
  }
  return FullSequence{current_group_id_, queue_.back().size() - 1};
}

}  // namespace moqt
