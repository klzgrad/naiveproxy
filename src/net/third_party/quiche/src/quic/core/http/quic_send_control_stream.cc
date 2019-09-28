// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"

namespace quic {

QuicSendControlStream::QuicSendControlStream(
    QuicStreamId id,
    QuicSession* session,
    uint64_t max_inbound_header_list_size)
    : QuicStream(id, session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      settings_sent_(false),
      max_inbound_header_list_size_(max_inbound_header_list_size) {}

void QuicSendControlStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset send control stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSendControlStream::SendSettingsFrame() {
  if (settings_sent_) {
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  // Send the stream type on so the peer knows about this stream.
  char data[sizeof(kControlStream)];
  QuicDataWriter writer(QUIC_ARRAYSIZE(data), data);
  writer.WriteVarInt62(kControlStream);
  WriteOrBufferData(QuicStringPiece(writer.data(), writer.length()), false,
                    nullptr);

  SettingsFrame settings;
  settings.values[kSettingsMaxHeaderListSize] = max_inbound_header_list_size_;
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      encoder_.SerializeSettingsFrame(settings, &buffer);
  QUIC_DVLOG(1) << "Control stream " << id() << " is writing settings frame "
                << settings;
  WriteOrBufferData(QuicStringPiece(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
  settings_sent_ = true;
}

void QuicSendControlStream::WritePriority(const PriorityFrame& priority) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  if (!settings_sent_) {
    SendSettingsFrame();
  }
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      encoder_.SerializePriorityFrame(priority, &buffer);
  QUIC_DVLOG(1) << "Control Stream " << id() << " is writing " << priority;
  WriteOrBufferData(QuicStringPiece(buffer.get(), frame_length), false,
                    nullptr);
}

}  // namespace quic
