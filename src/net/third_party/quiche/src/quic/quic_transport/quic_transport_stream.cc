// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/quic_transport/quic_transport_stream.h"

#include <sys/types.h>

#include "absl/strings/string_view.h"
#include "quic/core/quic_buffer_allocator.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"

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
                                          session->IsIncomingStream(id),
                                          session->version())),
      adapter_(session, this, sequencer()),
      session_interface_(session_interface) {}

WebTransportStream::ReadResult QuicTransportStream::Read(char* buffer,
                                                         size_t buffer_size) {
  if (!session_interface_->IsSessionReady()) {
    return ReadResult{0, false};
  }

  return adapter_.Read(buffer, buffer_size);
}

WebTransportStream::ReadResult QuicTransportStream::Read(std::string* output) {
  if (!session_interface_->IsSessionReady()) {
    return ReadResult{0, false};
  }

  return adapter_.Read(output);
}

bool QuicTransportStream::Write(absl::string_view data) {
  if (!CanWrite()) {
    return false;
  }

  return adapter_.Write(data);
}

bool QuicTransportStream::SendFin() {
  if (!CanWrite()) {
    return false;
  }

  return adapter_.SendFin();
}

bool QuicTransportStream::CanWrite() const {
  return session_interface_->IsSessionReady() && adapter_.CanWrite();
}

size_t QuicTransportStream::ReadableBytes() const {
  if (!session_interface_->IsSessionReady()) {
    return 0;
  }

  return adapter_.ReadableBytes();
}

void QuicTransportStream::OnDataAvailable() {
  adapter_.OnDataAvailable();
}

void QuicTransportStream::OnCanWriteNewData() {
  // Ensure the origin check has been completed, as the stream can be notified
  // about being writable before that.
  if (!CanWrite()) {
    return;
  }
  adapter_.OnCanWriteNewData();
}

}  // namespace quic
