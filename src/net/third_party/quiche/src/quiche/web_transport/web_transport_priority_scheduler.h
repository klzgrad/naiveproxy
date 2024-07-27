// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_PRIORITY_SCHEDULER_H_
#define QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_PRIORITY_SCHEDULER_H_

#include <cstddef>
#include <optional>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/common/btree_scheduler.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {

// Schedules the streams within a WebTransport session as defined by the W3C
// WebTransport API. Unlike the W3C API, there is no need to track groups
// manually: the group is created when a first stream with the associated group
// ID is registered, and it is deleted when the last stream associated with the
// group is unregistered.
class QUICHE_EXPORT PriorityScheduler {
 public:
  // Returns true if there are any streams registered.
  bool HasRegistered() const { return active_groups_.HasRegistered(); }
  // Returns true if there are any streams scheduled.
  bool HasScheduled() const { return active_groups_.HasScheduled(); }

  // Returns the number of currently scheduled streams.
  size_t NumScheduled() const {
    size_t total = 0;
    for (const auto& [group_id, group_scheduler] : per_group_schedulers_) {
      total += group_scheduler.NumScheduled();
    }
    return total;
  }

  // Registers the specified stream with the supplied priority.  The stream must
  // not be already registered.
  absl::Status Register(StreamId stream_id, const StreamPriority& priority);
  // Unregisters a previously registered stream.
  absl::Status Unregister(StreamId stream_id);
  // Alters the priority of an already registered stream.
  absl::Status UpdateSendOrder(StreamId stream_id, SendOrder new_send_order);
  absl::Status UpdateSendGroup(StreamId stream_id, SendGroupId new_send_group);

  // Returns true if there is a stream that would go before `id` in the
  // schedule.
  absl::StatusOr<bool> ShouldYield(StreamId id) const;

  // Returns the priority for `id`, or nullopt if stream is not registered.
  std::optional<StreamPriority> GetPriorityFor(StreamId stream_id) const;

  // Pops the highest priority stream.  Will fail if the schedule is empty.
  absl::StatusOr<StreamId> PopFront();

  // Adds `stream` to the schedule if it's not already there.
  absl::Status Schedule(StreamId stream_id);
  // Returns true if `stream` is in the schedule.
  bool IsScheduled(StreamId stream_id) const;

 private:
  // All groups currently have the equal priority; this type represents the said
  // single priority.
  class SinglePriority {
   public:
    bool operator==(const SinglePriority&) const { return true; }
    bool operator!=(const SinglePriority&) const { return false; }

    bool operator<(const SinglePriority&) const { return false; }
    bool operator>(const SinglePriority&) const { return false; }
    bool operator<=(const SinglePriority&) const { return true; }
    bool operator>=(const SinglePriority&) const { return true; }
  };

  using PerGroupScheduler = quiche::BTreeScheduler<StreamId, SendOrder>;
  using GroupSchedulerPair = std::pair<const SendGroupId, PerGroupScheduler>;

  // Round-robin schedule for the groups.
  quiche::BTreeScheduler<SendGroupId, SinglePriority> active_groups_;
  // Map group ID to the scheduler for the group in question.
  absl::node_hash_map<SendGroupId, PerGroupScheduler> per_group_schedulers_;
  // Map stream ID to a pointer to the entry in `per_group_schedulers_`.
  absl::flat_hash_map<StreamId, GroupSchedulerPair*> stream_to_group_map_;

  PerGroupScheduler* SchedulerForStream(StreamId stream_id) {
    auto it = stream_to_group_map_.find(stream_id);
    if (it == stream_to_group_map_.end()) {
      return nullptr;
    }
    return &it->second->second;
  }
  const PerGroupScheduler* SchedulerForStream(StreamId stream_id) const {
    auto it = stream_to_group_map_.find(stream_id);
    if (it == stream_to_group_map_.end()) {
      return nullptr;
    }
    return &it->second->second;
  }
};

}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_WEB_TRANSPORT_PRIORITY_SCHEDULER_H_
