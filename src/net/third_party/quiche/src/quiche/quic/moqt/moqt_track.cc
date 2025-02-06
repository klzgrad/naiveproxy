// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "quiche/quic/moqt/moqt_track.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

bool RemoteTrack::CheckDataStreamType(MoqtDataStreamType type) {
  if (data_stream_type_.has_value()) {
    return data_stream_type_.value() == type;
  }
  data_stream_type_ = type;
  return true;
}

UpstreamFetch::~UpstreamFetch() {
  if (task_.IsValid()) {
    // Notify the task (which the application owns) that nothing more is coming.
    // If this has already been called, UpstreamFetchTask will ignore it.
    task_.GetIfAvailable()->OnStreamAndFetchClosed(kResetCodeUnknown, "");
  }
}

void UpstreamFetch::OnFetchResult(FullSequence largest_id, absl::Status status,
                                  TaskDestroyedCallback callback) {
  auto task = std::make_unique<UpstreamFetchTask>(largest_id, status,
                                                  std::move(callback));
  task_ = task->weak_ptr();
  std::move(ok_callback_)(std::move(task));
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
    return kPending;
  }
  output = *std::move(next_object_);
  next_object_.reset();
  return kSuccess;
}

void UpstreamFetch::UpstreamFetchTask::NewObject(PublishedObject& object) {
  QUICHE_DCHECK(!next_object_.has_value());
  next_object_ = std::move(object);
  if (object_available_callback_) {
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
