// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_send_stream.h"

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_session.h"

namespace quic {
QpackSendStream::QpackSendStream(QuicStreamId id, QuicSession* session,
                                 uint64_t http3_stream_type)
    : QuicStream(id, session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      http3_stream_type_(http3_stream_type),
      stream_type_sent_(false) {}

void QpackSendStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  QUIC_BUG(quic_bug_10805_1)
      << "OnStreamReset() called for write unidirectional stream.";
}

bool QpackSendStream::OnStopSending(QuicResetStreamError /* code */) {
  stream_delegate()->OnStreamError(
      QUIC_HTTP_CLOSED_CRITICAL_STREAM,
      "STOP_SENDING received for QPACK send stream");
  return false;
}

void QpackSendStream::WriteStreamData(absl::string_view data) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendStreamType();
  WriteOrBufferData(data, false, nullptr);
}

uint64_t QpackSendStream::NumBytesBuffered() const {
  return QuicStream::BufferedDataBytes();
}

void QpackSendStream::MaybeSendStreamType() {
  if (!stream_type_sent_) {
    char type[sizeof(http3_stream_type_)];
    QuicDataWriter writer(ABSL_ARRAYSIZE(type), type);
    writer.WriteVarInt62(http3_stream_type_);
    WriteOrBufferData(absl::string_view(writer.data(), writer.length()), false,
                      nullptr);
    stream_type_sent_ = true;
  }
}

}  // namespace quic
