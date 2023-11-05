// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/web_transport/complete_buffer_visitor.h"

#include <utility>

#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_stream.h"

namespace webtransport {

void CompleteBufferVisitor::OnCanRead() {
  if (!incoming_data_callback_.has_value()) {
    return;
  }
  Stream::ReadResult result = stream_->Read(&incoming_data_buffer_);
  if (result.fin) {
    (*std::move(incoming_data_callback_))(std::move(incoming_data_buffer_));
    incoming_data_callback_.reset();
  }
}

void CompleteBufferVisitor::OnCanWrite() {
  if (!outgoing_data_.has_value()) {
    return;
  }
  if (!stream_->CanWrite()) {
    return;
  }
  quiche::StreamWriteOptions options;
  options.set_send_fin(true);
  absl::Status status =
      quiche::WriteIntoStream(*stream_, *outgoing_data_, options);
  if (!status.ok()) {
    QUICHE_DLOG(WARNING) << "Write from OnCanWrite() failed: " << status;
    return;
  }
  outgoing_data_.reset();
}

void CompleteBufferVisitor::SetOutgoingData(std::string data) {
  QUICHE_DCHECK(!outgoing_data_.has_value());
  outgoing_data_ = std::move(data);
  if (stream_->CanWrite()) {
    OnCanWrite();
  }
}

}  // namespace webtransport
