// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_request_update_queue.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"

namespace moqt {

void MoqtRequestUpdateQueue::Enqueue(const MessageParameters& parameters,
                                     MoqtResponseCallback callback) {
  pending_responses_.push_back(std::move(callback));
  pending_updates_.push_back(parameters);
}

absl::StatusOr<MessageParameters> MoqtRequestUpdateQueue::NextParameters()
    const {
  if (pending_updates_.empty()) {
    return absl::FailedPreconditionError(
        "REQUEST_OK received but no REQUEST_UPDATE corresponding to it");
  }
  return pending_updates_.front();
}

absl::Status MoqtRequestUpdateQueue::OnControlMessage(
    const MoqtRequestOk& message) {
  if (pending_responses_.empty() || pending_updates_.empty()) {
    return absl::FailedPreconditionError(
        "REQUEST_OK received but no REQUEST_UPDATE corresponding to it");
  }
  MoqtResponseCallback callback = std::move(pending_responses_.front());
  pending_responses_.pop_front();
  pending_updates_.pop_front();
  std::move(callback)(message.parameters);
  return absl::OkStatus();
}

absl::Status MoqtRequestUpdateQueue::OnControlMessage(
    const MoqtRequestError& message) {
  if (pending_responses_.empty()) {
    return absl::OkStatus();
  }
  std::move(pending_responses_.front())(MoqtRequestErrorInfo{
      message.error_code, message.retry_interval, message.reason_phrase});
  Clear();
  return absl::OkStatus();
}

}  // namespace moqt
