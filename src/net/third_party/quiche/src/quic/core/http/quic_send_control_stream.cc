// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

QuicSendControlStream::QuicSendControlStream(QuicStreamId id,
                                             QuicSpdySession* session)
    : QuicStream(id, session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      settings_sent_(false) {}

void QuicSendControlStream::OnStreamReset(const QuicRstStreamFrame& frame) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset send control stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSendControlStream::SendSettingsFrame(const SettingsFrame& settings) {
  DCHECK(!settings_sent_);
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      encoder_.SerializeSettingsFrame(settings, &buffer);
  WriteOrBufferData(QuicStringPiece(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
  settings_sent_ = true;
}

}  // namespace quic
