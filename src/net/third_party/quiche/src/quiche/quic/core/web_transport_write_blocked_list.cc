// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/web_transport_write_blocked_list.h"

#include <cstddef>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

bool WebTransportWriteBlockedList::HasWriteBlockedDataStreams() const {
  return main_schedule_.NumScheduledInPriorityRange(
             std::nullopt, RemapUrgency(HttpStreamPriority::kMaximumUrgency,
                                        /*is_http=*/true)) > 0;
}

size_t WebTransportWriteBlockedList::NumBlockedSpecialStreams() const {
  return main_schedule_.NumScheduledInPriorityRange(
      RemapUrgency(kStaticUrgency, /*is_http=*/false), std::nullopt);
}

size_t WebTransportWriteBlockedList::NumBlockedStreams() const {
  size_t num_streams = main_schedule_.NumScheduled();
  for (const auto& [key, scheduler] : web_transport_session_schedulers_) {
    if (scheduler.HasScheduled()) {
      num_streams += scheduler.NumScheduled();
      // Account for the fact that the group itself has an entry in the main
      // scheduler that does not correspond to any actual stream.
      QUICHE_DCHECK(main_schedule_.IsScheduled(key));
      --num_streams;
    }
  }
  return num_streams;
}

void WebTransportWriteBlockedList::RegisterStream(
    QuicStreamId stream_id, bool is_static_stream,
    const QuicStreamPriority& raw_priority) {
  QuicStreamPriority priority =
      is_static_stream
          ? QuicStreamPriority(HttpStreamPriority{kStaticUrgency, true})
          : raw_priority;
  auto [unused, success] = priorities_.emplace(stream_id, priority);
  if (!success) {
    QUICHE_BUG(WTWriteBlocked_RegisterStream_already_registered)
        << "Tried to register stream " << stream_id
        << " that is already registered";
    return;
  }

  if (priority.type() == QuicPriorityType::kHttp) {
    absl::Status status = main_schedule_.Register(
        ScheduleKey::HttpStream(stream_id),
        RemapUrgency(priority.http().urgency, /*is_http=*/true));
    QUICHE_BUG_IF(WTWriteBlocked_RegisterStream_http_scheduler, !status.ok())
        << status;
    return;
  }

  QUICHE_DCHECK_EQ(priority.type(), QuicPriorityType::kWebTransport);
  ScheduleKey group_key = ScheduleKey::WebTransportSession(priority);
  auto [it, created_new] =
      web_transport_session_schedulers_.try_emplace(group_key);
  absl::Status status =
      it->second.Register(stream_id, priority.web_transport().send_order);
  QUICHE_BUG_IF(WTWriteBlocked_RegisterStream_data_scheduler, !status.ok())
      << status;

  // If the group is new, register it with the main scheduler.
  if (created_new) {
    // The IETF draft requires the priority of data streams associated with an
    // individual session to be equivalent to the priority of the control
    // stream.
    auto session_priority_it =
        priorities_.find(priority.web_transport().session_id);
    // It is possible for a stream to be (re-)registered while the control
    // stream is already gone.
    QUICHE_DLOG_IF(WARNING, session_priority_it == priorities_.end())
        << "Stream " << stream_id << " is associated with session ID "
        << priority.web_transport().session_id
        << ", but the session control stream is not registered; assuming "
           "default urgency.";
    QuicStreamPriority session_priority =
        session_priority_it != priorities_.end() ? session_priority_it->second
                                                 : QuicStreamPriority();

    status = main_schedule_.Register(
        group_key,
        RemapUrgency(session_priority.http().urgency, /*is_http=*/false));
    QUICHE_BUG_IF(WTWriteBlocked_RegisterStream_main_scheduler, !status.ok())
        << status;
  }
}

void WebTransportWriteBlockedList::UnregisterStream(QuicStreamId stream_id) {
  auto map_it = priorities_.find(stream_id);
  if (map_it == priorities_.end()) {
    QUICHE_BUG(WTWriteBlocked_UnregisterStream_not_found)
        << "Stream " << stream_id << " not found";
    return;
  }
  QuicStreamPriority priority = map_it->second;
  priorities_.erase(map_it);

  if (priority.type() != QuicPriorityType::kWebTransport) {
    absl::Status status =
        main_schedule_.Unregister(ScheduleKey::HttpStream(stream_id));
    QUICHE_BUG_IF(WTWriteBlocked_UnregisterStream_http, !status.ok()) << status;
    return;
  }

  ScheduleKey key = ScheduleKey::WebTransportSession(priority);
  auto subscheduler_it = web_transport_session_schedulers_.find(key);
  if (subscheduler_it == web_transport_session_schedulers_.end()) {
    QUICHE_BUG(WTWriteBlocked_UnregisterStream_no_subscheduler)
        << "Stream " << stream_id
        << " is a WebTransport data stream, but has no scheduler for the "
           "associated group";
    return;
  }
  Subscheduler& subscheduler = subscheduler_it->second;
  absl::Status status = subscheduler.Unregister(stream_id);
  QUICHE_BUG_IF(WTWriteBlocked_UnregisterStream_subscheduler_stream_failed,
                !status.ok())
      << status;

  // If this is the last stream associated with the group, remove the group.
  if (!subscheduler.HasRegistered()) {
    status = main_schedule_.Unregister(key);
    QUICHE_BUG_IF(WTWriteBlocked_UnregisterStream_subscheduler_failed,
                  !status.ok())
        << status;

    web_transport_session_schedulers_.erase(subscheduler_it);
  }
}

void WebTransportWriteBlockedList::UpdateStreamPriority(
    QuicStreamId stream_id, const QuicStreamPriority& new_priority) {
  QuicStreamPriority old_priority = GetPriorityOfStream(stream_id);
  if (old_priority == new_priority) {
    return;
  }

  bool was_blocked = IsStreamBlocked(stream_id);
  UnregisterStream(stream_id);
  RegisterStream(stream_id, /*is_static_stream=*/false, new_priority);
  if (was_blocked) {
    AddStream(stream_id);
  }

  if (new_priority.type() == QuicPriorityType::kHttp) {
    for (auto& [key, subscheduler] : web_transport_session_schedulers_) {
      QUICHE_DCHECK(key.has_group());
      if (key.stream() == stream_id) {
        absl::Status status =
            main_schedule_.UpdatePriority(key, new_priority.http().urgency);
        QUICHE_BUG_IF(WTWriteBlocked_UpdateStreamPriority_subscheduler_failed,
                      !status.ok())
            << status;
      }
    }
  }
}

QuicStreamId WebTransportWriteBlockedList::PopFront() {
  absl::StatusOr<ScheduleKey> main_key = main_schedule_.PopFront();
  if (!main_key.ok()) {
    QUICHE_BUG(WTWriteBlocked_PopFront_no_streams)
        << "PopFront() called when no streams scheduled: " << main_key.status();
    return 0;
  }
  if (!main_key->has_group()) {
    return main_key->stream();
  }

  auto it = web_transport_session_schedulers_.find(*main_key);
  if (it == web_transport_session_schedulers_.end()) {
    QUICHE_BUG(WTWriteBlocked_PopFront_no_subscheduler)
        << "Subscheduler for WebTransport group " << main_key->DebugString()
        << " not found";
    return 0;
  }
  Subscheduler& subscheduler = it->second;
  absl::StatusOr<QuicStreamId> result = subscheduler.PopFront();
  if (!result.ok()) {
    QUICHE_BUG(WTWriteBlocked_PopFront_subscheduler_empty)
        << "Subscheduler for group " << main_key->DebugString()
        << " is empty while in the main schedule";
    return 0;
  }
  if (subscheduler.HasScheduled()) {
    absl::Status status = main_schedule_.Schedule(*main_key);
    QUICHE_BUG_IF(WTWriteBlocked_PopFront_reschedule_group, !status.ok())
        << status;
  }
  return *result;
}

void WebTransportWriteBlockedList::AddStream(QuicStreamId stream_id) {
  QuicStreamPriority priority = GetPriorityOfStream(stream_id);
  absl::Status status;
  switch (priority.type()) {
    case QuicPriorityType::kHttp:
      status = main_schedule_.Schedule(ScheduleKey::HttpStream(stream_id));
      QUICHE_BUG_IF(WTWriteBlocked_AddStream_http, !status.ok()) << status;
      break;
    case QuicPriorityType::kWebTransport:
      status =
          main_schedule_.Schedule(ScheduleKey::WebTransportSession(priority));
      QUICHE_BUG_IF(WTWriteBlocked_AddStream_wt_main, !status.ok()) << status;

      auto it = web_transport_session_schedulers_.find(
          ScheduleKey::WebTransportSession(priority));
      if (it == web_transport_session_schedulers_.end()) {
        QUICHE_BUG(WTWriteBlocked_AddStream_no_subscheduler)
            << ScheduleKey::WebTransportSession(priority);
        return;
      }
      Subscheduler& subscheduler = it->second;
      status = subscheduler.Schedule(stream_id);
      QUICHE_BUG_IF(WTWriteBlocked_AddStream_wt_sub, !status.ok()) << status;
      break;
  }
}

bool WebTransportWriteBlockedList::IsStreamBlocked(
    QuicStreamId stream_id) const {
  QuicStreamPriority priority = GetPriorityOfStream(stream_id);
  switch (priority.type()) {
    case QuicPriorityType::kHttp:
      return main_schedule_.IsScheduled(ScheduleKey::HttpStream(stream_id));
    case QuicPriorityType::kWebTransport:
      auto it = web_transport_session_schedulers_.find(
          ScheduleKey::WebTransportSession(priority));
      if (it == web_transport_session_schedulers_.end()) {
        QUICHE_BUG(WTWriteBlocked_IsStreamBlocked_no_subscheduler)
            << ScheduleKey::WebTransportSession(priority);
        return false;
      }
      const Subscheduler& subscheduler = it->second;
      return subscheduler.IsScheduled(stream_id);
  }
  QUICHE_NOTREACHED();
  return false;
}

QuicStreamPriority WebTransportWriteBlockedList::GetPriorityOfStream(
    QuicStreamId id) const {
  auto it = priorities_.find(id);
  if (it == priorities_.end()) {
    QUICHE_BUG(WTWriteBlocked_GetPriorityOfStream_not_found)
        << "Stream " << id << " not found";
    return QuicStreamPriority();
  }
  return it->second;
}

std::string WebTransportWriteBlockedList::ScheduleKey::DebugString() const {
  return absl::StrFormat("(%d, %d)", stream_, group_);
}

bool WebTransportWriteBlockedList::ShouldYield(QuicStreamId id) const {
  QuicStreamPriority priority = GetPriorityOfStream(id);
  if (priority.type() == QuicPriorityType::kHttp) {
    absl::StatusOr<bool> should_yield =
        main_schedule_.ShouldYield(ScheduleKey::HttpStream(id));
    QUICHE_BUG_IF(WTWriteBlocked_ShouldYield_http, !should_yield.ok())
        << should_yield.status();
    return *should_yield;
  }
  QUICHE_DCHECK_EQ(priority.type(), QuicPriorityType::kWebTransport);
  absl::StatusOr<bool> should_yield =
      main_schedule_.ShouldYield(ScheduleKey::WebTransportSession(priority));
  QUICHE_BUG_IF(WTWriteBlocked_ShouldYield_wt_main, !should_yield.ok())
      << should_yield.status();
  if (*should_yield) {
    return true;
  }

  auto it = web_transport_session_schedulers_.find(
      ScheduleKey::WebTransportSession(priority));
  if (it == web_transport_session_schedulers_.end()) {
    QUICHE_BUG(WTWriteBlocked_ShouldYield_subscheduler_not_found)
        << "Subscheduler not found for "
        << ScheduleKey::WebTransportSession(priority);
    return false;
  }
  const Subscheduler& subscheduler = it->second;

  should_yield = subscheduler.ShouldYield(id);
  QUICHE_BUG_IF(WTWriteBlocked_ShouldYield_wt_subscheduler, !should_yield.ok())
      << should_yield.status();
  return *should_yield;
}
}  // namespace quic
