// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_outgoing_queue.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_stream_map.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt {

namespace {
void ObjectsInDescendingOrder(std::vector<Location>& objects) {
  absl::c_reverse(objects);
  for (auto it = objects.begin(); it != objects.end();) {
    auto start_it = it;
    while (it != objects.end() && it->group == start_it->group) {
      ++it;
    }
    std::reverse(start_it, it);
  }
}
}  // namespace

void MoqtOutgoingQueue::AddObject(quiche::QuicheMemSlice payload, bool key) {
  if (queue_.empty() && !key) {
    QUICHE_BUG(MoqtOutgoingQueue_AddObject_first_object_not_key)
        << "The first object ever added to the queue must have the \"key\" "
           "flag.";
    return;
  }
  if (closed_) {
    QUICHE_BUG(MoqtOutgoingQueue_AddObject_closed)
        << "Trying to send objects on a closed queue.";
    return;
  }

  if (key) {
    OpenNewGroup();
  }
  AddRawObject(MoqtObjectStatus::kNormal, std::move(payload));
}

void MoqtOutgoingQueue::OpenNewGroup() {
  if (!queue_.empty()) {
    AddRawObject(MoqtObjectStatus::kEndOfGroup, quiche::QuicheMemSlice());
  }

  if (queue_.size() == kMaxQueuedGroups) {
    queue_.erase(queue_.begin());
    for (MoqtObjectListener* listener : listeners_) {
      listener->OnGroupAbandoned(current_group_id_ - kMaxQueuedGroups + 1);
    }
  }
  queue_.emplace_back();
  ++current_group_id_;
}

void MoqtOutgoingQueue::AddRawObject(MoqtObjectStatus status,
                                     quiche::QuicheMemSlice payload) {
  Location sequence{current_group_id_, queue_.back().size()};
  bool fin = status == MoqtObjectStatus::kEndOfGroup;
  queue_.back().push_back(CachedObject{
      PublishedObjectMetadata{sequence, 0, "", status,
                              default_publisher_priority(),
                              clock_->ApproximateNow()},
      std::make_shared<quiche::QuicheMemSlice>(std::move(payload)), fin});
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(sequence, /*subgroup=*/0,
                                   default_publisher_priority());
  }
}

std::optional<PublishedObject> MoqtOutgoingQueue::GetCachedObject(
    uint64_t group, std::optional<uint64_t> subgroup, uint64_t object) const {
  QUICHE_DCHECK(subgroup.has_value() && subgroup == 0u);
  if (group < first_group_in_queue()) {
    return std::nullopt;
  }
  if (group > current_group_id_) {
    return std::nullopt;
  }
  const std::vector<CachedObject>& group_objects =
      queue_[group - first_group_in_queue()];
  if (object >= group_objects.size()) {
    return std::nullopt;
  }
  QUICHE_DCHECK(Location(group, object) ==
                group_objects[object].metadata.location);
  return CachedObjectToPublishedObject(group_objects[object]);
}

std::vector<Location> MoqtOutgoingQueue::GetCachedObjectsInRange(
    Location start, Location end) const {
  std::vector<Location> sequences;
  for (const Group& group : queue_) {
    for (const CachedObject& object : group) {
      if (object.metadata.location >= start &&
          object.metadata.location <= end) {
        sequences.push_back(object.metadata.location);
      }
    }
  }
  return sequences;
}

std::optional<Location> MoqtOutgoingQueue::largest_location() const {
  if (queue_.empty()) {
    return std::nullopt;
  }
  return Location{current_group_id_, queue_.back().size() - 1};
}

std::unique_ptr<MoqtFetchTask> MoqtOutgoingQueue::StandaloneFetch(
    Location start, Location end, MoqtDeliveryOrder order) {
  if (queue_.empty()) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("No objects available on the track"));
  }

  Location first_available_object = Location(first_group_in_queue(), 0);
  Location last_available_object =
      Location(current_group_id_, queue_.back().size() - 1);

  if (end < first_available_object) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("All of the requested objects have expired"));
  }
  if (start > last_available_object) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("All of the requested objects are in the future"));
  }

  Location adjusted_start = std::max(start, first_available_object);
  Location adjusted_end = std::min(end, last_available_object);
  std::vector<Location> objects =
      GetCachedObjectsInRange(adjusted_start, adjusted_end);
  // Default to ascending order.
  if (order == MoqtDeliveryOrder::kDescending) {
    ObjectsInDescendingOrder(objects);
  }
  return std::make_unique<FetchTask>(this, std::move(objects));
}

std::unique_ptr<MoqtFetchTask> MoqtOutgoingQueue::RelativeFetch(
    uint64_t /*group_diff*/, MoqtDeliveryOrder /*order*/) {
  QUICHE_BUG(MoqtOutgoingQueue_RelativeFetch)
      << "Calling RelativeFetch() on an established subscription";
  return std::make_unique<MoqtFailedFetch>(absl::InternalError(
      "RelativeFetch called on an established subscription"));
}

std::unique_ptr<MoqtFetchTask> MoqtOutgoingQueue::AbsoluteFetch(
    uint64_t /*group*/, MoqtDeliveryOrder /*order*/) {
  QUICHE_BUG(MoqtOutgoingQueue_AbsoluteFetch)
      << "Calling AbsoluteFetch() on an established subscription";
  return std::make_unique<MoqtFailedFetch>(absl::InternalError(
      "AbsoluteFetch called on an established subscription"));
}

MoqtFetchTask::GetNextObjectResult MoqtOutgoingQueue::FetchTask::GetNextObject(
    PublishedObject& object) {
  if (!status_.ok()) {
    return kError;
  }
  while (!objects_.empty()) {
    std::optional<PublishedObject> new_object = queue_->GetCachedObject(
        objects_.front().group, 0, objects_.front().object);
    objects_.pop_front();
    if (new_object.has_value() &&
        new_object->metadata.status == MoqtObjectStatus::kNormal) {
      object = *std::move(new_object);
      return kSuccess;
    }
  }
  return kEof;
}

void MoqtOutgoingQueue::Close() {
  if (closed_) {
    QUICHE_BUG(MoqtOutgoingQueue_Close_twice)
        << "Trying to close an outgoing queue that is already closed.";
    return;
  }
  closed_ = true;

  OpenNewGroup();
  AddRawObject(MoqtObjectStatus::kEndOfTrack, {});
}

}  // namespace moqt
