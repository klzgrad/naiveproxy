// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_stream.h"

#include <sys/types.h>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {

QuicTransportStream::QuicTransportStream(
    QuicStreamId id,
    QuicSession* session,
    QuicTransportSessionInterface* session_interface)
    : QuicStream(id,
                 session,
                 /*is_static=*/false,
                 QuicUtils::GetStreamType(id,
                                          session->connection()->perspective(),
                                          session->IsIncomingStream(id))),
      session_interface_(session_interface) {}

size_t QuicTransportStream::Read(char* buffer, size_t buffer_size) {
  if (!session_interface_->IsSessionReady()) {
    return 0;
  }

  iovec iov;
  iov.iov_base = buffer;
  iov.iov_len = buffer_size;
  const size_t result = sequencer()->Readv(&iov, 1);
  if (sequencer()->IsClosed() && visitor_ != nullptr) {
    visitor_->OnFinRead();
  }
  return result;
}

size_t QuicTransportStream::Read(std::string* output) {
  const size_t old_size = output->size();
  const size_t bytes_to_read = ReadableBytes();
  output->resize(old_size + bytes_to_read);
  size_t bytes_read = Read(&(*output)[old_size], bytes_to_read);
  DCHECK_EQ(bytes_to_read, bytes_read);
  output->resize(old_size + bytes_read);
  return bytes_read;
}

bool QuicTransportStream::Write(QuicStringPiece data) {
  if (!CanWrite()) {
    return false;
  }

  // TODO(vasilvv): use WriteMemSlices()
  WriteOrBufferData(data, /*fin=*/false, nullptr);
  return true;
}

bool QuicTransportStream::SendFin() {
  if (!CanWrite()) {
    return false;
  }

  WriteOrBufferData(QuicStringPiece(), /*fin=*/true, nullptr);
  return true;
}

bool QuicTransportStream::CanWrite() const {
  return session_interface_->IsSessionReady() && CanWriteNewData();
}

size_t QuicTransportStream::ReadableBytes() const {
  if (!session_interface_->IsSessionReady()) {
    return 0;
  }

  return sequencer()->ReadableBytes();
}

void QuicTransportStream::OnDataAvailable() {
  if (sequencer()->IsClosed()) {
    if (visitor_ != nullptr) {
      visitor_->OnFinRead();
    }
    OnFinRead();
    return;
  }

  if (visitor_ == nullptr) {
    return;
  }
  if (ReadableBytes() == 0) {
    return;
  }
  visitor_->OnCanRead();
}

void QuicTransportStream::OnCanWriteNewData() {
  // Ensure the origin check has been completed, as the stream can be notified
  // about being writable before that.
  if (!CanWrite()) {
    return;
  }
  if (visitor_ != nullptr) {
    visitor_->OnCanWrite();
  }
}

}  // namespace quic
