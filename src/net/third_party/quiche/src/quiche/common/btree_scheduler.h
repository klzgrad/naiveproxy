// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_BTREE_SCHEDULER_H_
#define QUICHE_COMMON_BTREE_SCHEDULER_H_

#include <limits>

#include "absl/container/btree_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// BTreeScheduler is a data structure that allows streams (and potentially other
// entities) to be scheduled according to the arbitrary priorities.  The API for
// using the scheduler can be used as follows:
//  - A stream has to be registered with a priority before being scheduled.
//  - A stream can be unregistered, or can be re-prioritized.
//  - A stream can be scheduled; that adds it into the queue.
//  - PopFront() will return the stream with highest priority.
//  - ShouldYield() will return if there is a stream with higher priority than
//    the specified one.
//
// The prioritization works as following:
//  - If two streams have different priorities, the higher priority stream goes
//    first.
//  - If two streams have the same priority, the one that got scheduled earlier
//    goes first. Internally, this is implemented by assigning a monotonically
//    decreasing sequence number to every newly scheduled stream.
//
// The Id type has to define operator==, be hashable via absl::Hash, and
// printable via operator<<; the Priority type has to define operator<.
template <typename Id, typename Priority>
class QUICHE_EXPORT BTreeScheduler {
 public:
  // Returns true if there are any streams scheduled.
  bool HasScheduled() const { return !schedule_.empty(); }
  // Returns the number of currently scheduled streams.
  size_t NumScheduled() const { return schedule_.size(); }

  // Counts the number of scheduled entries in the range [min, max].  If either
  // min or max is omitted, negative or positive infinity is assumed.
  size_t NumScheduledInPriorityRange(absl::optional<Priority> min,
                                     absl::optional<Priority> max) const;

  // Returns true if there is a stream that would go before `id` in the
  // schedule.
  absl::StatusOr<bool> ShouldYield(Id id) const;

  // Returns the priority for `id`, or nullopt if stream is not registered.
  absl::optional<Priority> GetPriorityFor(Id id) const {
    auto it = streams_.find(id);
    if (it == streams_.end()) {
      return absl::nullopt;
    }
    return it->second.priority;
  }

  // Pops the highest priority stream.  Will fail if the schedule is empty.
  absl::StatusOr<Id> PopFront();

  // Registers the specified stream with the supplied priority.  The stream must
  // not be already registered.
  absl::Status Register(Id stream_id, const Priority& priority);
  // Unregisters a previously registered stream.
  absl::Status Unregister(Id stream_id);
  // Alters the priority of an already registered stream.
  absl::Status UpdatePriority(Id stream_id, const Priority& new_priority);

  // Adds the `stream` into the schedule if it's not already there.
  absl::Status Schedule(Id stream_id);
  // Returns true if `stream` is in the schedule.
  bool IsScheduled(Id stream_id) const;

 private:
  // A record for a registered stream.
  struct StreamEntry {
    // The current priority of the stream.
    Priority priority;
    // If present, the sequence number with which the stream is currently
    // scheduled.  If absent, indicates that the stream is not scheduled.
    absl::optional<int> current_sequence_number;

    bool scheduled() const { return current_sequence_number.has_value(); }
  };
  // The full entry for the stream (includes the ID that's used as a hashmap
  // key).
  using FullStreamEntry = std::pair<const Id, StreamEntry>;

  // A key that is used to order entities within the schedule.
  struct ScheduleKey {
    // The main order key: the priority of the stream.
    Priority priority;
    // The secondary order key: the sequence number.
    int sequence_number;

    // Orders schedule keys in order of decreasing priority.
    bool operator<(const ScheduleKey& other) const {
      return std::make_tuple(priority, sequence_number) >
             std::make_tuple(other.priority, other.sequence_number);
    }

    // In order to find all entities with priority `p`, one can iterate between
    // `lower_bound(MinForPriority(p))` and `upper_bound(MaxForPriority(p))`.
    static ScheduleKey MinForPriority(Priority priority) {
      return ScheduleKey{priority, std::numeric_limits<int>::max()};
    }
    static ScheduleKey MaxForPriority(Priority priority) {
      return ScheduleKey{priority, std::numeric_limits<int>::min()};
    }
  };
  using FullScheduleEntry = std::pair<const ScheduleKey, FullStreamEntry*>;
  using ScheduleIterator =
      typename absl::btree_map<ScheduleKey, FullStreamEntry*>::const_iterator;

  // Convenience method to get the stream ID for a schedule entry.
  static Id StreamId(const FullScheduleEntry& entry) {
    return entry.second->first;
  }

  // Removes a stream from the schedule, and returns the old entry if it were
  // present.
  absl::StatusOr<FullScheduleEntry> DescheduleStream(const StreamEntry& entry);

  // The map of currently registered streams.
  absl::node_hash_map<Id, StreamEntry> streams_;
  // The stream schedule, ordered starting from the highest priority stream.
  absl::btree_map<ScheduleKey, FullStreamEntry*> schedule_;

  // The counter that is used to ensure that streams with the same priority are
  // handled in the FIFO order.  Decreases with every write.
  int current_write_sequence_number_ = 0;
};

template <typename Id, typename Priority>
size_t BTreeScheduler<Id, Priority>::NumScheduledInPriorityRange(
    absl::optional<Priority> min, absl::optional<Priority> max) const {
  if (min.has_value() && max.has_value()) {
    QUICHE_DCHECK(*min <= *max);
  }
  // This is reversed, since the schedule is ordered in the descending priority
  // order.
  ScheduleIterator begin =
      max.has_value() ? schedule_.lower_bound(ScheduleKey::MinForPriority(*max))
                      : schedule_.begin();
  ScheduleIterator end =
      min.has_value() ? schedule_.upper_bound(ScheduleKey::MaxForPriority(*min))
                      : schedule_.end();
  return end - begin;
}

template <typename Id, typename Priority>
absl::Status BTreeScheduler<Id, Priority>::Register(Id stream_id,
                                                    const Priority& priority) {
  auto [it, success] = streams_.insert({stream_id, StreamEntry{priority}});
  if (!success) {
    return absl::AlreadyExistsError("ID already registered");
  }
  return absl::OkStatus();
}

template <typename Id, typename Priority>
auto BTreeScheduler<Id, Priority>::DescheduleStream(const StreamEntry& entry)
    -> absl::StatusOr<FullScheduleEntry> {
  QUICHE_DCHECK(entry.scheduled());
  auto it = schedule_.find(
      ScheduleKey{entry.priority, *entry.current_sequence_number});
  if (it == schedule_.end()) {
    return absl::InternalError(
        "Calling DescheduleStream() on an entry that is not in the schedule at "
        "the expected key.");
  }
  FullScheduleEntry result = *it;
  schedule_.erase(it);
  return result;
}

template <typename Id, typename Priority>
absl::Status BTreeScheduler<Id, Priority>::Unregister(Id stream_id) {
  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    return absl::NotFoundError("Stream not registered");
  }
  const StreamEntry& stream = it->second;

  if (stream.scheduled()) {
    if (!DescheduleStream(stream).ok()) {
      QUICHE_BUG(BTreeSchedule_Unregister_NotInSchedule)
          << "UnregisterStream() called on a stream ID " << stream_id
          << ", which is marked ready, but is not in the schedule";
    }
  }

  streams_.erase(it);
  return absl::OkStatus();
}

template <typename Id, typename Priority>
absl::Status BTreeScheduler<Id, Priority>::UpdatePriority(
    Id stream_id, const Priority& new_priority) {
  auto it = streams_.find(stream_id);
  if (it == streams_.end()) {
    return absl::NotFoundError("ID not registered");
  }

  StreamEntry& stream = it->second;
  absl::optional<int> sequence_number;
  if (stream.scheduled()) {
    absl::StatusOr<FullScheduleEntry> old_entry = DescheduleStream(stream);
    if (old_entry.ok()) {
      sequence_number = old_entry->first.sequence_number;
      QUICHE_DCHECK_EQ(old_entry->second, &*it);
    } else {
      QUICHE_BUG(BTreeScheduler_Update_Not_In_Schedule)
          << "UpdatePriority() called on a stream ID " << stream_id
          << ", which is marked ready, but is not in the schedule";
    }
  }

  stream.priority = new_priority;
  if (sequence_number.has_value()) {
    schedule_.insert({ScheduleKey{stream.priority, *sequence_number}, &*it});
  }
  return absl::OkStatus();
}

template <typename Id, typename Priority>
absl::StatusOr<bool> BTreeScheduler<Id, Priority>::ShouldYield(
    Id stream_id) const {
  const auto stream_it = streams_.find(stream_id);
  if (stream_it == streams_.end()) {
    return absl::NotFoundError("ID not registered");
  }
  const StreamEntry& stream = stream_it->second;

  if (schedule_.empty()) {
    return false;
  }
  const FullScheduleEntry& next = *schedule_.begin();
  if (StreamId(next) == stream_id) {
    return false;
  }
  return next.first.priority >= stream.priority;
}

template <typename Id, typename Priority>
absl::StatusOr<Id> BTreeScheduler<Id, Priority>::PopFront() {
  if (schedule_.empty()) {
    return absl::NotFoundError("No streams scheduled");
  }
  auto schedule_it = schedule_.begin();
  QUICHE_DCHECK(schedule_it->second->second.scheduled());
  schedule_it->second->second.current_sequence_number = absl::nullopt;

  Id result = StreamId(*schedule_it);
  schedule_.erase(schedule_it);
  return result;
}

template <typename Id, typename Priority>
absl::Status BTreeScheduler<Id, Priority>::Schedule(Id stream_id) {
  const auto stream_it = streams_.find(stream_id);
  if (stream_it == streams_.end()) {
    return absl::NotFoundError("ID not registered");
  }
  if (stream_it->second.scheduled()) {
    return absl::OkStatus();
  }
  auto [schedule_it, success] =
      schedule_.insert({ScheduleKey{stream_it->second.priority,
                                    --current_write_sequence_number_},
                        &*stream_it});
  QUICHE_BUG_IF(WebTransportWriteBlockedList_AddStream_conflict, !success)
      << "Conflicting key in scheduler for stream " << stream_id;
  stream_it->second.current_sequence_number =
      schedule_it->first.sequence_number;
  return absl::OkStatus();
}

template <typename Id, typename Priority>
bool BTreeScheduler<Id, Priority>::IsScheduled(Id stream_id) const {
  const auto stream_it = streams_.find(stream_id);
  if (stream_it == streams_.end()) {
    return false;
  }
  return stream_it->second.scheduled();
}

}  // namespace quiche

#endif  // QUICHE_COMMON_BTREE_SCHEDULER_H_
