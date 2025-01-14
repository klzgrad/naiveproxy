// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_outgoing_queue.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_cached_object.h"
#include "quiche/quic/moqt/moqt_failed_fetch.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
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
      sequence, status, publisher_priority_,
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
                           publisher_priority_, quiche::QuicheMemSlice()};
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
                           publisher_priority_, quiche::QuicheMemSlice()};
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

std::unique_ptr<MoqtFetchTask> MoqtOutgoingQueue::Fetch(
    FullSequence start, uint64_t end_group, std::optional<uint64_t> end_object,
    MoqtDeliveryOrder order) {
  if (queue_.empty()) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("No objects available on the track"));
  }

  FullSequence end = FullSequence(
      end_group, end_object.value_or(std::numeric_limits<uint64_t>::max()));
  FullSequence first_available_object = FullSequence(first_group_in_queue(), 0);
  FullSequence last_available_object =
      FullSequence(current_group_id_, queue_.back().size() - 1);

  if (end < first_available_object) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("All of the requested objects have expired"));
  }
  if (start > last_available_object) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("All of the requested objects are in the future"));
  }

  FullSequence adjusted_start = std::max(start, first_available_object);
  FullSequence adjusted_end = std::min(end, last_available_object);
  std::vector<FullSequence> objects =
      GetCachedObjectsInRange(adjusted_start, adjusted_end);
  if (order == MoqtDeliveryOrder::kDescending) {
    absl::c_reverse(objects);
    for (auto it = objects.begin(); it != objects.end();) {
      auto start_it = it;
      while (it != objects.end() && it->group == start_it->group) {
        ++it;
      }
      std::reverse(start_it, it);
    }
  }
  return std::make_unique<FetchTask>(this, std::move(objects));
}

MoqtFetchTask::GetNextObjectResult MoqtOutgoingQueue::FetchTask::GetNextObject(
    PublishedObject& object) {
  for (;;) {
    // The specification for FETCH requires that all missing objects are simply
    // skipped.
    MoqtFetchTask::GetNextObjectResult result = GetNextObjectInner(object);
    bool missing_object =
        result == kSuccess &&
        (object.status == MoqtObjectStatus::kObjectDoesNotExist ||
         object.status == MoqtObjectStatus::kGroupDoesNotExist);
    if (!missing_object) {
      return result;
    }
  }
}

MoqtFetchTask::GetNextObjectResult
MoqtOutgoingQueue::FetchTask::GetNextObjectInner(PublishedObject& object) {
  if (!status_.ok()) {
    return kError;
  }
  if (objects_.empty()) {
    return kEof;
  }

  std::optional<PublishedObject> result =
      queue_->GetCachedObject(objects_.front());
  if (!result.has_value()) {
    status_ = absl::InternalError("Previously known object became unknown.");
    return kError;
  }

  object = *std::move(result);
  objects_.pop_front();
  return kSuccess;
}

}  // namespace moqt
