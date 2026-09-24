// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_control_message_queue.h"

#include <array>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/web_transport/stream_helpers.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

absl::Status MoqtControlMessageQueue::OnCanWrite() {
  if (stream_ == nullptr) {
    return absl::InternalError("OnCanWrite() called when no stream is bound");
  }
  if (pending_messages_.empty() && fin_queued_) {
    return webtransport::SendFinOnStream(*stream_);
  }
  while (!pending_messages_.empty() && stream_->CanWrite()) {
    absl::Status status =
        SendMessage(*stream_, std::move(pending_messages_.front()),
                    fin_queued_ && pending_messages_.size() == 1);
    pending_messages_.pop_front();
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status MoqtControlMessageQueue::SendOrBufferMessage(
    quiche::QuicheBuffer message, bool fin) {
  if (fin_queued_) {
    return absl::InternalError(
        "Trying to send data when a FIN has been already queued");
  }
  if (stream_ == nullptr || !stream_->CanWrite()) {
    fin_queued_ = fin;
    return AddToQueue(std::move(message));
  }
  if (fin) {
    fin_queued_ = true;
  }
  return SendMessage(*stream_, std::move(message), fin);
}

absl::Status MoqtControlMessageQueue::Fin() {
  if (fin_queued_) {
    return absl::OkStatus();
  }
  fin_queued_ = true;
  if (stream_ != nullptr) {
    return OnCanWrite();
  }
  return absl::OkStatus();
}

absl::Status MoqtControlMessageQueue::AddToQueue(quiche::QuicheBuffer message) {
  if (pending_messages_.size() == kMaxPendingMessages) {
    return absl::ResourceExhaustedError(
        "Not enough flow credit on the control stream");
  }
  pending_messages_.push_back(std::move(message));
  return absl::OkStatus();
}

// static
absl::Status MoqtControlMessageQueue::SendMessage(webtransport::Stream& stream,
                                                  quiche::QuicheBuffer message,
                                                  bool fin) {
  webtransport::StreamWriteOptions options;
  options.set_send_fin(fin);
  std::array write_vector = {quiche::QuicheMemSlice(std::move(message))};
  return stream.Writev(absl::MakeSpan(write_vector), options);
}

}  // namespace moqt
