// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_live_relay_queue.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_cached_object.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace moqt {

// TODO(martinduke): Accept subgroup ID.
// TODO(martinduke): Accept publisher priority.
// TODO(martinduke): Unless Track Forwarding preference goes away, support it.
bool MoqtLiveRelayQueue::AddObject(uint64_t group_id, uint64_t object_id,
                                   MoqtObjectStatus status,
                                   absl::string_view object) {
  if (queue_.size() == kMaxQueuedGroups) {
    if (queue_.begin()->first > group_id) {
      QUICHE_DLOG(INFO) << "Skipping object from group " << group_id
                        << " because it is too old.";
      return true;
    }
    if (queue_.find(group_id) == queue_.end()) {
      // Erase the oldest group.
      queue_.erase(queue_.begin());
    }
  }
  QUICHE_CHECK(status == MoqtObjectStatus::kNormal || object.empty());
  return AddRawObject(FullSequence{group_id, object_id}, status, object);
}

std::tuple<uint64_t, bool> MoqtLiveRelayQueue::NextObject(Group& group) const {
  auto it = group.rbegin();
  if (it == group.rend()) {
    return std::tuple<uint64_t, bool>(0, false);
  }
  return std::tuple<uint64_t, bool>(
      it->second.sequence.object + 1,
      (it->second.status == MoqtObjectStatus::kEndOfGroup ||
       it->second.status == MoqtObjectStatus::kGroupDoesNotExist ||
       it->second.status == MoqtObjectStatus::kEndOfTrack));
}

bool MoqtLiveRelayQueue::AddRawObject(FullSequence sequence,
                                      MoqtObjectStatus status,
                                      absl::string_view payload) {
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
  if (!group_it.second) {  // Group already exists.
    auto [next_object_id, is_the_end] = NextObject(group_it.first->second);
    if (next_object_id <= sequence.object && is_the_end) {
      QUICHE_DLOG(INFO) << "Skipping object because it is after the end of the "
                        << "group";
      return false;
    }
    if (status == MoqtObjectStatus::kEndOfGroup &&
        sequence.object < next_object_id) {
      QUICHE_DLOG(INFO) << "Skipping EndOfGroup because it is not the last "
                        << "object in the group.";
      return false;
    }
  }
  if (next_sequence_ <= sequence) {
    next_sequence_ = FullSequence{sequence.group, sequence.object + 1};
  }
  std::shared_ptr<quiche::QuicheMemSlice> slice =
      payload.empty()
          ? nullptr
          : std::make_shared<quiche::QuicheMemSlice>(quiche::QuicheBuffer::Copy(
                quiche::SimpleBufferAllocator::Get(), payload));
  auto object_it = group_it.first->second.try_emplace(sequence.object, sequence,
                                                      status, slice);
  if (!object_it.second) {
    QUICHE_DLOG(ERROR) << "Sender is overwriting an existing object.";
    return false;
  }
  for (MoqtObjectListener* listener : listeners_) {
    listener->OnNewObjectAvailable(sequence);
  }
  return true;
}

std::optional<PublishedObject> MoqtLiveRelayQueue::GetCachedObject(
    FullSequence sequence) const {
  auto group_it = queue_.find(sequence.group);
  if (group_it == queue_.end()) {
    return std::nullopt;
  }
  auto object_it = group_it->second.find(sequence.object);
  if (object_it == group_it->second.end()) {
    return std::nullopt;
  }
  return CachedObjectToPublishedObject(object_it->second);
}

std::vector<FullSequence> MoqtLiveRelayQueue::GetCachedObjectsInRange(
    FullSequence start, FullSequence end) const {
  std::vector<FullSequence> sequences;
  SubscribeWindow window(start, end);
  for (auto& group_it : queue_) {
    for (auto& object_it : group_it.second) {
      if (window.InWindow(object_it.second.sequence)) {
        sequences.push_back(object_it.second.sequence);
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
