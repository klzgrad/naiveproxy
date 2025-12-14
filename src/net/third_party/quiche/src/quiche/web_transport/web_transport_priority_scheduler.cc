// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/web_transport/web_transport_priority_scheduler.h"

#include <optional>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {

absl::Status PriorityScheduler::Register(StreamId stream_id,
                                         const StreamPriority& priority) {
  auto [it, success] = stream_to_group_map_.insert({stream_id, nullptr});
  if (!success) {
    return absl::AlreadyExistsError("Provided stream ID already registered");
  }
  // Avoid having any nullptr entries in the stream map if we error out further
  // down below. This should not happen (all errors below are logical errors),
  // but if that does happen, we will avoid crashing due to nullptr dereference.
  auto cleanup_nullptr_map_entry =
      absl::MakeCleanup([&] { stream_to_group_map_.erase(stream_id); });

  auto [scheduler_it, scheduler_created] =
      per_group_schedulers_.try_emplace(priority.send_group_id);
  if (scheduler_created) {
    // First element in the associated group; register the group in question.
    QUICHE_RETURN_IF_ERROR(active_groups_.Register(priority.send_group_id, {}));
  }

  PerGroupScheduler& scheduler = scheduler_it->second;
  QUICHE_RETURN_IF_ERROR(scheduler.Register(stream_id, priority.send_order));

  it->second = &*scheduler_it;
  std::move(cleanup_nullptr_map_entry).Cancel();
  return absl::OkStatus();
}

absl::Status PriorityScheduler::Unregister(StreamId stream_id) {
  auto it = stream_to_group_map_.find(stream_id);
  if (it == stream_to_group_map_.end()) {
    return absl::NotFoundError("Stream ID not registered");
  }
  SendGroupId group_id = it->second->first;
  PerGroupScheduler* group_scheduler = &it->second->second;
  stream_to_group_map_.erase(it);

  QUICHE_RETURN_IF_ERROR(group_scheduler->Unregister(stream_id));
  // Clean up the group if there are no more streams associated with it.
  if (!group_scheduler->HasRegistered()) {
    per_group_schedulers_.erase(group_id);
    QUICHE_RETURN_IF_ERROR(active_groups_.Unregister(group_id));
  }
  return absl::OkStatus();
}

absl::Status PriorityScheduler::UpdateSendOrder(StreamId stream_id,
                                                SendOrder new_send_order) {
  PerGroupScheduler* scheduler = SchedulerForStream(stream_id);
  if (scheduler == nullptr) {
    return absl::NotFoundError("Stream ID not registered");
  }
  return scheduler->UpdatePriority(stream_id, new_send_order);
}

absl::Status PriorityScheduler::UpdateSendGroup(StreamId stream_id,
                                                SendGroupId new_send_group) {
  PerGroupScheduler* scheduler = SchedulerForStream(stream_id);
  if (scheduler == nullptr) {
    return absl::NotFoundError("Stream ID not registered");
  }
  bool is_scheduled = scheduler->IsScheduled(stream_id);
  std::optional<SendOrder> send_order = scheduler->GetPriorityFor(stream_id);
  if (!send_order.has_value()) {
    return absl::InternalError(
        "Stream registered at the top level scheduler, but not at the "
        "per-group one");
  }
  QUICHE_RETURN_IF_ERROR(Unregister(stream_id));
  QUICHE_RETURN_IF_ERROR(
      Register(stream_id, StreamPriority{new_send_group, *send_order}));
  if (is_scheduled) {
    QUICHE_RETURN_IF_ERROR(Schedule(stream_id));
  }
  return absl::OkStatus();
}

std::optional<StreamPriority> PriorityScheduler::GetPriorityFor(
    StreamId stream_id) const {
  auto it = stream_to_group_map_.find(stream_id);
  if (it == stream_to_group_map_.end()) {
    return std::nullopt;
  }
  const auto& [group_id, group_scheduler] = *it->second;
  std::optional<SendOrder> send_order =
      group_scheduler.GetPriorityFor(stream_id);
  if (!send_order.has_value()) {
    return std::nullopt;
  }
  return StreamPriority{group_id, *send_order};
}

absl::StatusOr<bool> PriorityScheduler::ShouldYield(StreamId stream_id) const {
  auto it = stream_to_group_map_.find(stream_id);
  if (it == stream_to_group_map_.end()) {
    return absl::NotFoundError("Stream ID not registered");
  }
  const auto& [group_id, group_scheduler] = *it->second;

  absl::StatusOr<bool> per_group_result = active_groups_.ShouldYield(group_id);
  QUICHE_RETURN_IF_ERROR(per_group_result.status());
  if (*per_group_result) {
    return true;
  }

  return group_scheduler.ShouldYield(stream_id);
}

absl::StatusOr<StreamId> PriorityScheduler::PopFront() {
  absl::StatusOr<SendGroupId> group_id = active_groups_.PopFront();
  QUICHE_RETURN_IF_ERROR(group_id.status());

  auto it = per_group_schedulers_.find(*group_id);
  if (it == per_group_schedulers_.end()) {
    return absl::InternalError(
        "Scheduled a group with no per-group scheduler attached");
  }
  PerGroupScheduler& scheduler = it->second;
  absl::StatusOr<StreamId> result = scheduler.PopFront();
  if (!result.ok()) {
    return absl::InternalError("Inactive group found in top-level schedule");
  }

  // Reschedule the group if it has more active streams in it.
  if (scheduler.HasScheduled()) {
    QUICHE_RETURN_IF_ERROR(active_groups_.Schedule(*group_id));
  }

  return result;
}

absl::Status PriorityScheduler::Schedule(StreamId stream_id) {
  auto it = stream_to_group_map_.find(stream_id);
  if (it == stream_to_group_map_.end()) {
    return absl::NotFoundError("Stream ID not registered");
  }
  auto& [group_id, group_scheduler] = *it->second;
  QUICHE_RETURN_IF_ERROR(active_groups_.Schedule(group_id));
  return group_scheduler.Schedule(stream_id);
}

bool PriorityScheduler::IsScheduled(StreamId stream_id) const {
  const PerGroupScheduler* scheduler = SchedulerForStream(stream_id);
  if (scheduler == nullptr) {
    return false;
  }
  return scheduler->IsScheduled(stream_id);
}

}  // namespace webtransport
