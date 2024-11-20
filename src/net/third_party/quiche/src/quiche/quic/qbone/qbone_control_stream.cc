// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_control_stream.h"

#include <cstdint>
#include <limits>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/qbone/qbone_constants.h"

namespace quic {

namespace {
static constexpr size_t kRequestSizeBytes = sizeof(uint16_t);
}  // namespace

QboneControlStreamBase::QboneControlStreamBase(QuicSession* session)
    : QuicStream(
          QboneConstants::GetControlStreamId(session->transport_version()),
          session,
          /*is_static=*/true, BIDIRECTIONAL),
      pending_message_size_(0) {}

QboneControlStreamBase::QboneControlStreamBase(quic::PendingStream* pending,
                                               QuicSession* session)
    : QuicStream(pending, session, /*is_static=*/true),
      pending_message_size_(0) {
  QUICHE_DCHECK_EQ(pending->id(), QboneConstants::GetControlStreamId(
                                      session->transport_version()));
}

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
    std::string tmp = buffer_.substr(0, pending_message_size_);
    buffer_.erase(0, pending_message_size_);
    pending_message_size_ = 0;
    OnMessage(tmp);
  }
}

bool QboneControlStreamBase::SendMessage(const proto2::Message& proto) {
  std::string tmp;
  if (!proto.SerializeToString(&tmp)) {
    QUIC_BUG(quic_bug_11023_1) << "Failed to serialize QboneControlRequest";
    return false;
  }
  if (tmp.size() > std::numeric_limits<uint16_t>::max()) {
    QUIC_BUG(quic_bug_11023_2)
        << "QboneControlRequest too large: " << tmp.size() << " > "
        << std::numeric_limits<uint16_t>::max();
    return false;
  }
  uint16_t size = tmp.size();
  char size_str[kRequestSizeBytes];
  memcpy(size_str, &size, kRequestSizeBytes);
  WriteOrBufferData(absl::string_view(size_str, kRequestSizeBytes), false,
                    nullptr);
  WriteOrBufferData(tmp, false, nullptr);
  return true;
}

void QboneControlStreamBase::OnStreamReset(
    const QuicRstStreamFrame& /*frame*/) {
  stream_delegate()->OnStreamError(QUIC_INVALID_STREAM_ID,
                                   "Attempt to reset control stream");
}

}  // namespace quic
