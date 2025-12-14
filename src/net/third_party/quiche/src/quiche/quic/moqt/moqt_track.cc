// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "quiche/quic/moqt/moqt_track.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

constexpr quic::QuicTimeDelta kMinPublishDoneTimeout =
    quic::QuicTimeDelta::FromSeconds(1);
constexpr quic::QuicTimeDelta kMaxPublishDoneTimeout =
    quic::QuicTimeDelta::FromSeconds(10);

}  // namespace

bool RemoteTrack::CheckDataStreamType(MoqtDataStreamType type) {
  if (is_fetch() && !type.IsFetch()) {
    return false;
  }
  if (!is_fetch() && !type.IsSubgroup()) {
    return false;
  }
  return true;
}

void SubscribeRemoteTrack::OnStreamOpened() {
  ++currently_open_streams_;
  if (subscribe_done_alarm_ != nullptr && subscribe_done_alarm_->IsSet()) {
    subscribe_done_alarm_->Cancel();
  }
}

void SubscribeRemoteTrack::OnStreamClosed(
    bool fin_received, std::optional<DataStreamIndex> index) {
  ++streams_closed_;
  --currently_open_streams_;
  QUICHE_DCHECK_GE(currently_open_streams_, -1);
  if (index.has_value()) {
    // If index is nullopt, there was not an object received on the stream.
    if (fin_received) {
      visitor_->OnStreamFin(full_track_name(), *index);
    } else {
      visitor_->OnStreamReset(full_track_name(), *index);
    }
  }
  if (subscribe_done_alarm_ == nullptr) {
    return;
  }
  MaybeSetPublishDoneAlarm();
}

void SubscribeRemoteTrack::OnPublishDone(
    uint64_t stream_count, const quic::QuicClock* clock,
    std::unique_ptr<quic::QuicAlarm> subscribe_done_alarm) {
  total_streams_ = stream_count;
  clock_ = clock;
  subscribe_done_alarm_ = std::move(subscribe_done_alarm);
  MaybeSetPublishDoneAlarm();
}

void SubscribeRemoteTrack::MaybeSetPublishDoneAlarm() {
  if (currently_open_streams_ == 0 && total_streams_.has_value() &&
      clock_ != nullptr) {
    quic::QuicTimeDelta timeout =
        std::min(delivery_timeout_, kMaxPublishDoneTimeout);
    timeout = std::max(timeout, kMinPublishDoneTimeout);
    subscribe_done_alarm_->Set(clock_->ApproximateNow() + timeout);
  }
}

void SubscribeRemoteTrack::OnJoiningFetchReady(
    std::unique_ptr<MoqtFetchTask> fetch_task) {
  fetch_task_ = std::move(fetch_task);
  fetch_task_->SetObjectAvailableCallback([this]() { FetchObjects(); });
  FetchObjects();
}

void SubscribeRemoteTrack::FetchObjects() {
  if (fetch_task_ == nullptr) {
    return;
  }
  if (visitor_ == nullptr || !fetch_task_->GetStatus().ok()) {
    fetch_task_.reset();
    return;
  }
  while (true) {
    PublishedObject object;
    switch (fetch_task_->GetNextObject(object)) {
      case MoqtFetchTask::GetNextObjectResult::kSuccess:
        visitor_->OnObjectFragment(full_track_name(), object.metadata,
                                   object.payload.AsStringView(), true);
        break;
      case MoqtFetchTask::GetNextObjectResult::kError:
      case MoqtFetchTask::GetNextObjectResult::kEof:
        fetch_task_.reset();
        return;
      case MoqtFetchTask::GetNextObjectResult::kPending:
        return;
    }
  }
}

UpstreamFetch::~UpstreamFetch() {
  if (task_.IsValid()) {
    // Notify the task (which the application owns) that nothing more is coming.
    // If this has already been called, UpstreamFetchTask will ignore it.
    task_.GetIfAvailable()->OnStreamAndFetchClosed(kResetCodeUnknown, "");
  }
}

void UpstreamFetch::OnFetchResult(Location largest_location,
                                  MoqtDeliveryOrder group_order,
                                  absl::Status status,
                                  TaskDestroyedCallback callback) {
  if (group_order_.has_value()) {
    // Data stream already implied a group order.
    if (*group_order_ != group_order) {
      // The track is malformed. Tell the application it failed.
      std::move(ok_callback_)(
          std::make_unique<MoqtFailedFetch>(MoqtStreamErrorToStatus(
              kResetCodeMalformedTrack, "Group order violation")));
      // Tell the session this failed, so it can cancel the FETCH.
      std::move(callback)();
      return;
    }
  } else {
    group_order_ = group_order;
  }
  if (!status.ok()) {
    std::move(ok_callback_)(std::make_unique<MoqtFailedFetch>(status));
    // This is called from OnFetchError, which will delete UpstreamFetch. So
    // there is no need to call |callback|, which would inappropriately send a
    // FETCH_CANCEL.
    return;
  }
  auto task = std::make_unique<UpstreamFetchTask>(largest_location, status,
                                                  std::move(callback));
  task_ = task->weak_ptr();
  window_mutable().TruncateEnd(largest_location);
  std::move(ok_callback_)(std::move(task));
  if (can_read_callback_) {
    task_.GetIfAvailable()->set_can_read_callback(
        std::move(can_read_callback_));
  }
}

void UpstreamFetch::OnStreamOpened(CanReadCallback can_read_callback) {
  if (task_.IsValid()) {
    task_.GetIfAvailable()->set_can_read_callback(std::move(can_read_callback));
  } else {
    can_read_callback_ = std::move(can_read_callback);
  }
}

bool UpstreamFetch::LocationIsValid(Location location, MoqtObjectStatus status,
                                    bool end_of_message) {
  if (end_of_track_.has_value()) {
    // Cannot exceed or change end_of_track_.
    if (location > end_of_track_) {
      return false;
    }
    if (status == MoqtObjectStatus::kEndOfTrack && location != *end_of_track_) {
      return false;
    }
  }
  if (end_of_message && status == MoqtObjectStatus::kEndOfTrack) {
    if (highest_location_.has_value() && location < *highest_location_) {
      return false;
    }
    end_of_track_ = location;
  }
  bool last_group_is_finished = last_group_is_finished_;
  last_group_is_finished_ =
      status == MoqtObjectStatus::kEndOfGroup && end_of_message;
  std::optional<Location> last_location = last_location_;
  if (end_of_message) {
    last_location_ = location;
    if (!highest_location_.has_value()) {
      highest_location_ = location;
    } else {
      highest_location_ = std::max(*highest_location_, location);
    }
  }
  if (!last_location.has_value()) {
    return true;
  }
  if (last_location->group == location.group) {
    return (!last_group_is_finished && location.object > last_location->object);
  }
  // Group ID has changed.
  if (!group_order_.has_value()) {
    group_order_ = location.group > last_location->group
                       ? MoqtDeliveryOrder::kAscending
                       : MoqtDeliveryOrder::kDescending;
    return true;
  }
  return ((location.group > last_location->group) ==
          (*group_order_ == MoqtDeliveryOrder::kAscending));
}

UpstreamFetch::UpstreamFetchTask::~UpstreamFetchTask() {
  if (task_destroyed_callback_) {
    std::move(task_destroyed_callback_)();
  }
}

MoqtFetchTask::GetNextObjectResult
UpstreamFetch::UpstreamFetchTask::GetNextObject(PublishedObject& output) {
  if (!next_object_.has_value()) {
    if (!status_.ok()) {
      return kError;
    }
    if (eof_) {
      return kEof;
    }
    need_object_available_callback_ = true;
    return kPending;
  }
  if (!payload_.empty()) {
    quiche::QuicheMemSlice message_slice(std::move(payload_));
    output.payload = std::move(message_slice);
  }
  output.metadata.location =
      Location(next_object_->group_id, next_object_->object_id);
  output.metadata.subgroup = next_object_->subgroup_id;
  output.metadata.status = next_object_->object_status;
  output.metadata.publisher_priority = next_object_->publisher_priority;
  output.fin_after_this = false;
  if (output.metadata.location ==
      largest_location_) {  // This is the last object.
    eof_ = true;
  }
  next_object_.reset();
  can_read_callback_();
  return kSuccess;
}

void UpstreamFetch::UpstreamFetchTask::NewObject(const MoqtObject& message) {
  next_object_ = message;
  payload_ = quiche::QuicheBuffer(quiche::SimpleBufferAllocator::Get(),
                                  message.payload_length);
}

void UpstreamFetch::UpstreamFetchTask::AppendPayloadToObject(
    absl::string_view payload) {
  QUICHE_BUG_IF(quic_bug_AppendPayloadToObjectCalledEarly,
                !next_object_.has_value())
      << "AppendPayloadToObject called without an object";
  QUICHE_BUG_IF(quic_bug_AlreadyGotPayload, next_object_->payload_length == 0)
      << "AppendPayloadToObject called after payload was already full";
  // Copy |payload| to the right spot in the buffer.
  memcpy(payload_.data() + payload_.size() - next_object_->payload_length,
         payload.data(), payload.length());
  next_object_->payload_length -= payload.length();
}

void UpstreamFetch::UpstreamFetchTask::NotifyNewObject() {
  QUICHE_BUG_IF(quic_bug_NotifyNewObjectCalledEarly,
                !next_object_.has_value() || next_object_->payload_length > 0)
      << "NotifyNewObject called without a full object in store";
  if (need_object_available_callback_ && object_available_callback_) {
    need_object_available_callback_ = false;
    object_available_callback_();
  }
}

void UpstreamFetch::UpstreamFetchTask::OnStreamAndFetchClosed(
    std::optional<webtransport::StreamErrorCode> error,
    absl::string_view reason_phrase) {
  if (eof_ || !status_.ok()) {
    return;
  }
  // Delete callbacks, because IncomingDataStream and UpstreamFetch are gone.
  can_read_callback_ = nullptr;
  task_destroyed_callback_ = nullptr;
  if (!error.has_value()) {  // This was a FIN.
    eof_ = true;
  } else {
    status_ = MoqtStreamErrorToStatus(*error, reason_phrase);
  }
  if (object_available_callback_) {
    object_available_callback_();
  }
}

}  // namespace moqt
