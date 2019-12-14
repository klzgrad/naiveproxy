// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"

namespace quic {

QuicSendControlStream::QuicSendControlStream(
    QuicStreamId id,
    QuicSession* session,
    uint64_t qpack_maximum_dynamic_table_capacity,
    uint64_t qpack_maximum_blocked_streams,
    uint64_t max_inbound_header_list_size)
    : QuicStream(id, session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      settings_sent_(false),
      qpack_maximum_dynamic_table_capacity_(
          qpack_maximum_dynamic_table_capacity),
      qpack_maximum_blocked_streams_(qpack_maximum_blocked_streams),
      max_inbound_header_list_size_(max_inbound_header_list_size) {}

void QuicSendControlStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset send control stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSendControlStream::MaybeSendSettingsFrame() {
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
  settings.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] =
      qpack_maximum_dynamic_table_capacity_;
  settings.values[SETTINGS_QPACK_BLOCKED_STREAMS] =
      qpack_maximum_blocked_streams_;
  settings.values[SETTINGS_MAX_HEADER_LIST_SIZE] =
      max_inbound_header_list_size_;

  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      encoder_.SerializeSettingsFrame(settings, &buffer);
  QUIC_DVLOG(1) << "Control stream " << id() << " is writing settings frame "
                << settings;
  QuicSpdySession* spdy_session = static_cast<QuicSpdySession*>(session());
  if (spdy_session->debug_visitor() != nullptr) {
    spdy_session->debug_visitor()->OnSettingsFrameSent(settings);
  }
  WriteOrBufferData(QuicStringPiece(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
  settings_sent_ = true;
}

void QuicSendControlStream::WritePriority(const PriorityFrame& priority) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      encoder_.SerializePriorityFrame(priority, &buffer);
  QUIC_DVLOG(1) << "Control Stream " << id() << " is writing " << priority;
  WriteOrBufferData(QuicStringPiece(buffer.get(), frame_length), false,
                    nullptr);
}

void QuicSendControlStream::SendMaxPushIdFrame(PushId max_push_id) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());

  MaybeSendSettingsFrame();
  MaxPushIdFrame frame;
  frame.push_id = max_push_id;
  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length = encoder_.SerializeMaxPushIdFrame(frame, &buffer);
  WriteOrBufferData(QuicStringPiece(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
}

}  // namespace quic
