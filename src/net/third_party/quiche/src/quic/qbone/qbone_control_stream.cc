// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_control_stream.h"

#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"

namespace quic {

namespace {
static constexpr size_t kRequestSizeBytes = sizeof(uint16_t);
}  // namespace

QboneControlStreamBase::QboneControlStreamBase(QuicSession* session)
    : QuicStream(QboneConstants::GetControlStreamId(
                     session->connection()->transport_version()),
                 session,
                 /*is_static=*/true,
                 BIDIRECTIONAL),
      pending_message_size_(0) {}

void QboneControlStreamBase::OnDataAvailable() {
  sequencer()->Read(&buffer_);
  while (true) {
    if (pending_message_size_ == 0) {
      // Start of a message.
      if (buffer_.size() < kRequestSizeBytes) {
        return;
      }
      memcpy(&pending_message_size_, buffer_.data(), kRequestSizeBytes);
      buffer_.erase(0, kRequestSizeBytes);
    }
    // Continuation of a message.
    if (buffer_.size() < pending_message_size_) {
      return;
    }
    string tmp = buffer_.substr(0, pending_message_size_);
    buffer_.erase(0, pending_message_size_);
    pending_message_size_ = 0;
    OnMessage(tmp);
  }
}

bool QboneControlStreamBase::SendMessage(const proto2::Message& proto) {
  string tmp;
  if (!proto.SerializeToString(&tmp)) {
    QUIC_BUG << "Failed to serialize QboneControlRequest";
    return false;
  }
  if (tmp.size() > kuint16max) {
    QUIC_BUG << "QboneControlRequest too large: " << tmp.size() << " > "
             << kuint16max;
    return false;
  }
  uint16_t size = tmp.size();
  char size_str[kRequestSizeBytes];
  memcpy(size_str, &size, kRequestSizeBytes);
  WriteOrBufferData(QuicStringPiece(size_str, kRequestSizeBytes), false,
                    nullptr);
  WriteOrBufferData(tmp, false, nullptr);
  return true;
}

}  // namespace quic
