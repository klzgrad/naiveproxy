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
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

constexpr quic::QuicTimeDelta kMinSubscribeDoneTimeout =
    quic::QuicTimeDelta::FromSeconds(1);
constexpr quic::QuicTimeDelta kMaxSubscribeDoneTimeout =
    quic::QuicTimeDelta::FromSeconds(10);

}  // namespace

bool RemoteTrack::CheckDataStreamType(MoqtDataStreamType type) {
  if (data_stream_type_.has_value()) {
    return data_stream_type_.value() == type;
  }
  data_stream_type_ = type;
  return true;
}

void SubscribeRemoteTrack::OnStreamOpened() {
  ++currently_open_streams_;
  if (subscribe_done_alarm_ != nullptr && subscribe_done_alarm_->IsSet()) {
    subscribe_done_alarm_->Cancel();
  }
}

void SubscribeRemoteTrack::OnStreamClosed() {
  ++streams_closed_;
  --currently_open_streams_;
  QUICHE_DCHECK_GE(currently_open_streams_, -1);
  if (subscribe_done_alarm_ == nullptr) {
    return;
  }
  MaybeSetSubscribeDoneAlarm();
}

void SubscribeRemoteTrack::OnSubscribeDone(
    uint64_t stream_count, const quic::QuicClock* clock,
    std::unique_ptr<quic::QuicAlarm> subscribe_done_alarm) {
  total_streams_ = stream_count;
  clock_ = clock;
  subscribe_done_alarm_ = std::move(subscribe_done_alarm);
  MaybeSetSubscribeDoneAlarm();
}

void SubscribeRemoteTrack::MaybeSetSubscribeDoneAlarm() {
  if (currently_open_streams_ == 0 && total_streams_.has_value() &&
      clock_ != nullptr) {
    quic::QuicTimeDelta timeout =
        std::min(delivery_timeout_, kMaxSubscribeDoneTimeout);
    timeout = std::max(timeout, kMinSubscribeDoneTimeout);
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
        visitor_->OnObjectFragment(full_track_name(), object.sequence,
                                   object.publisher_priority, object.status,
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
                                  absl::Status status,
                                  TaskDestroyedCallback callback) {
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
  output.sequence =
      Location(next_object_->group_id, next_object_->subgroup_id.value_or(0),
               next_object_->object_id);
  output.status = next_object_->object_status;
  output.publisher_priority = next_object_->publisher_priority;
  output.fin_after_this = false;
  if (output.sequence == largest_location_) {  // This is the last object.
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
  if (eof_ || error.has_value()) {
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
