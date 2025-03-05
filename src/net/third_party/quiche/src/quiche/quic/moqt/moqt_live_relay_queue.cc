// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_live_relay_queue.h"

#include <memory>
#include <optional>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_cached_object.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt {

bool MoqtLiveRelayQueue::AddFin(FullSequence sequence) {
  switch (forwarding_preference_) {
    case MoqtForwardingPreference::kDatagram:
      return false;
    case MoqtForwardingPreference::kSubgroup:
      break;
  }
  auto group_it = queue_.find(sequence.group);
  if (group_it == queue_.end()) {
    // Group does not exist.
    return false;
  }
  Group& group = group_it->second;
  auto subgroup_it = group.subgroups.find(
      SubgroupPriority{publisher_priority_, sequence.subgroup});
  if (subgroup_it == group.subgroups.end()) {
    // Subgroup does not exist.
    return false;
  }
  if (subgroup_it->second.empty()) {
    // Cannot FIN an empty subgroup.
    return false;
  }
  if (subgroup_it->second.rbegin()->first != sequence.object) {
    // The queue does not yet have the last object.
    return false;
  }
  subgroup_it->second.rbegin()->second.fin_after_this = true;
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewFinAvailable(sequence);
  }
  return true;
}

// TODO(martinduke): Unless Track Forwarding preference goes away, support it.
bool MoqtLiveRelayQueue::AddRawObject(FullSequence sequence,
                                      MoqtObjectStatus status,
                                      MoqtPriority priority,
                                      absl::string_view payload, bool fin) {
  bool last_object_in_stream = fin;
  if (queue_.size() == kMaxQueuedGroups) {
    if (queue_.begin()->first > sequence.group) {
      QUICHE_DLOG(INFO) << "Skipping object from group " << sequence.group
                        << " because it is too old.";
      return true;
    }
    if (queue_.find(sequence.group) == queue_.end()) {
      // Erase the oldest group.
      for (MoqtObjectListener* listener : listeners_) {
        listener->OnGroupAbandoned(queue_.begin()->first);
      }
      queue_.erase(queue_.begin());
    }
  }
  // Validate the input given previously received markers.
  if (end_of_track_.has_value() && sequence > *end_of_track_) {
    QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                      << "track";
    return false;
  }
  if (status == MoqtObjectStatus::kEndOfTrack) {
    if (sequence < next_sequence_) {
      QUICHE_DLOG(INFO) << "EndOfTrack is too early.";
      return false;
    }
    // TODO(martinduke): Check that EndOfTrack has normal IDs.
    end_of_track_ = sequence;
  }
  if (status == MoqtObjectStatus::kGroupDoesNotExist && sequence.object > 0) {
    QUICHE_DLOG(INFO) << "GroupDoesNotExist is not the last object in the "
                      << "group";
    return false;
  }
  auto group_it = queue_.try_emplace(sequence.group);
  Group& group = group_it.first->second;
  if (!group_it.second) {  // Group already exists.
    if (group.complete && sequence.object >= group.next_object) {
      QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                        << "group";
      return false;
    }
    if (status == MoqtObjectStatus::kEndOfGroup &&
        sequence.object < group.next_object) {
      QUICHE_DLOG(INFO) << "Skipping EndOfGroup because it is not the last "
                        << "object in the group.";
      return false;
    }
  }
  auto subgroup_it = group.subgroups.try_emplace(
      SubgroupPriority{priority, sequence.subgroup});
  auto& subgroup = subgroup_it.first->second;
  if (!subgroup.empty()) {  // Check if the new object is valid
    CachedObject& last_object = subgroup.rbegin()->second;
    if (last_object.fin_after_this) {
      QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                        << "subgroup";
      return false;
    }
    // If last_object has stream-ending status, it should have been caught by
    // the fin_after_this check above.
    QUICHE_DCHECK(last_object.status != MoqtObjectStatus::kEndOfSubgroup &&
                  last_object.status != MoqtObjectStatus::kEndOfGroup &&
                  last_object.status != MoqtObjectStatus::kEndOfTrack);
    if (last_object.sequence.object >= sequence.object) {
      QUICHE_DLOG(INFO) << "Skipping object because it does not increase the "
                        << "object ID monotonically in the subgroup.";
      return false;
    }
  }
  // Object is valid. Update state.
  if (next_sequence_ <= sequence) {
    next_sequence_ = FullSequence{sequence.group, sequence.object + 1};
  }
  if (sequence.object >= group.next_object) {
    group.next_object = sequence.object + 1;
  }
  // Anticipate stream FIN with most non-normal objects.
  switch (status) {
    case MoqtObjectStatus::kEndOfTrack:
      end_of_track_ = sequence;
      ABSL_FALLTHROUGH_INTENDED;
    case MoqtObjectStatus::kGroupDoesNotExist:
    case MoqtObjectStatus::kEndOfGroup:
      group.complete = true;
      ABSL_FALLTHROUGH_INTENDED;
    case MoqtObjectStatus::kEndOfSubgroup:
      last_object_in_stream = true;
      break;
    default:
      break;
  }
  std::shared_ptr<quiche::QuicheMemSlice> slice =
      payload.empty()
          ? nullptr
          : std::make_shared<quiche::QuicheMemSlice>(quiche::QuicheBuffer::Copy(
                quiche::SimpleBufferAllocator::Get(), payload));
  subgroup.emplace(sequence.object, CachedObject{sequence, status, priority,
                                                 slice, last_object_in_stream});
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(sequence);
  }
  return true;
}

std::optional<PublishedObject> MoqtLiveRelayQueue::GetCachedObject(
    FullSequence sequence) const {
  auto group_it = queue_.find(sequence.group);
  if (group_it == queue_.end()) {
    // Group does not exist.
    return std::nullopt;
  }
  const Group& group = group_it->second;
  auto subgroup_it = group.subgroups.find(
      SubgroupPriority{publisher_priority_, sequence.subgroup});
  if (subgroup_it == group.subgroups.end()) {
    // Subgroup does not exist.
    return std::nullopt;
  }
  const Subgroup& subgroup = subgroup_it->second;
  if (subgroup.empty()) {
    return std::nullopt;  // There are no objects.
  }
  // Find an object with ID of at least sequence.object.
  auto object_it = subgroup.lower_bound(sequence.object);
  if (object_it == subgroup.end()) {
    // No object after the last one received.
    return std::nullopt;
  }
  return CachedObjectToPublishedObject(object_it->second);
}

std::vector<FullSequence> MoqtLiveRelayQueue::GetCachedObjectsInRange(
    FullSequence start, FullSequence end) const {
  std::vector<FullSequence> sequences;
  SubscribeWindow window(start, end);
  for (auto& group_it : queue_) {
    if (group_it.first < start.group) {
      continue;
    }
    if (group_it.first > end.group) {
      return sequences;
    }
    for (auto& subgroup_it : group_it.second.subgroups) {
      for (auto& object_it : subgroup_it.second) {
        if (window.InWindow(object_it.second.sequence)) {
          sequences.push_back(object_it.second.sequence);
        }
        if (group_it.first == end.group &&
            object_it.second.sequence.object >= end.object) {
          break;
        }
      }
    }
  }
  return sequences;
}

absl::StatusOr<MoqtTrackStatusCode> MoqtLiveRelayQueue::GetTrackStatus() const {
  if (end_of_track_.has_value()) {
    return MoqtTrackStatusCode::kFinished;
  }
  if (queue_.empty()) {
    // TODO(martinduke): Retrieve the track status from upstream.
    return MoqtTrackStatusCode::kNotYetBegun;
  }
  return MoqtTrackStatusCode::kInProgress;
}

FullSequence MoqtLiveRelayQueue::GetLargestSequence() const {
  return FullSequence{next_sequence_.group, next_sequence_.object - 1};
}

}  // namespace moqt
