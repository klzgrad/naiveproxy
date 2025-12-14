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
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
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
  bool fin = forwarding_preference_ == MoqtForwardingPreference::kSubgroup &&
             status == MoqtObjectStatus::kEndOfGroup;
  queue_.back().push_back(CachedObject{
      PublishedObjectMetadata{sequence, 0, "", status, publisher_priority_,
                              clock_->ApproximateNow()},
      std::make_shared<quiche::QuicheMemSlice>(std::move(payload)), fin});
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(sequence, /*subgroup=*/0,
                                   publisher_priority_);
  }
}

std::optional<PublishedObject> MoqtOutgoingQueue::GetCachedObject(
    uint64_t group, uint64_t subgroup, uint64_t object) const {
  QUICHE_DCHECK_EQ(subgroup, 0u);
  if (group < first_group_in_queue()) {
    if (object == 0) {
      return PublishedObject{PublishedObjectMetadata{
                                 Location(group, object), /*subgroup=*/0, "",
                                 MoqtObjectStatus::kEndOfGroup,
                                 publisher_priority_, clock_->ApproximateNow()},
                             quiche::QuicheMemSlice{}};
    }
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
  SubscribeWindow window(start, end);
  for (const Group& group : queue_) {
    for (const CachedObject& object : group) {
      if (window.InWindow(object.metadata.location)) {
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
    Location start, Location end, std::optional<MoqtDeliveryOrder> order) {
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
    uint64_t group_diff, std::optional<MoqtDeliveryOrder> order) {
  if (queue_.empty()) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("No objects available on the track"));
  }

  uint64_t start_group = (group_diff > first_group_in_queue())
                             ? 0
                             : current_group_id_ - group_diff;
  start_group = std::max(start_group, first_group_in_queue());
  Location start = Location(start_group, 0);
  Location end = Location(current_group_id_, queue_.back().size() - 1);

  std::vector<Location> objects = GetCachedObjectsInRange(start, end);
  if (order == MoqtDeliveryOrder::kDescending) {
    ObjectsInDescendingOrder(objects);
  }
  return std::make_unique<FetchTask>(this, std::move(objects));
}

std::unique_ptr<MoqtFetchTask> MoqtOutgoingQueue::AbsoluteFetch(
    uint64_t group, std::optional<MoqtDeliveryOrder> order) {
  if (queue_.empty()) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("No objects available on the track"));
  }

  Location start(std::max(group, first_group_in_queue()), 0);
  Location end = Location(current_group_id_, queue_.back().size() - 1);
  if (start > end) {
    return std::make_unique<MoqtFailedFetch>(
        absl::NotFoundError("All of the requested objects are in the future"));
  }

  std::vector<Location> objects = GetCachedObjectsInRange(start, end);
  if (order == MoqtDeliveryOrder::kDescending) {
    ObjectsInDescendingOrder(objects);
  }
  return std::make_unique<FetchTask>(this, std::move(objects));
}

MoqtFetchTask::GetNextObjectResult MoqtOutgoingQueue::FetchTask::GetNextObject(
    PublishedObject& object) {
  MoqtFetchTask::GetNextObjectResult result;
  do {
    result = GetNextObjectInner(object);
    // The specification for FETCH requires that all missing objects are simply
    // skipped.
  } while (result == MoqtFetchTask::GetNextObjectResult::kSuccess &&
           object.metadata.status == MoqtObjectStatus::kObjectDoesNotExist);
  return result;
}

MoqtFetchTask::GetNextObjectResult
MoqtOutgoingQueue::FetchTask::GetNextObjectInner(PublishedObject& object) {
  if (!status_.ok()) {
    return kError;
  }
  if (objects_.empty()) {
    return kEof;
  }

  std::optional<PublishedObject> result = queue_->GetCachedObject(
      objects_.front().group, 0, objects_.front().object);
  if (!result.has_value()) {
    // Create a synthetic object of status kEndOfGroup (if the object ID is
    // zero) or kObjectDoesNotExist, which will result in the Fetch response
    // skipping it.
    object.metadata.location = objects_.front();
    object.metadata.subgroup = 0;
    object.metadata.publisher_priority = queue_->publisher_priority_;
    object.metadata.status = object.metadata.location.object == 0
                                 ? MoqtObjectStatus::kEndOfGroup
                                 : MoqtObjectStatus::kObjectDoesNotExist;
    object.metadata.arrival_time = queue_->clock_->ApproximateNow();
    object.payload = quiche::QuicheMemSlice();
    object.fin_after_this = false;
  } else {
    object = *std::move(result);
  }
  objects_.pop_front();
  return kSuccess;
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
