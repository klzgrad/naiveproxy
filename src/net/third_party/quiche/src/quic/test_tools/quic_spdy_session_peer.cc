// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_receive_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {
namespace test {

// static
QuicHeadersStream* QuicSpdySessionPeer::GetHeadersStream(
    QuicSpdySession* session) {
  DCHECK(!VersionUsesQpack(session->connection()->transport_version()));
  return session->headers_stream();
}

void QuicSpdySessionPeer::SetHeadersStream(QuicSpdySession* session,
                                           QuicHeadersStream* headers_stream) {
  DCHECK(!VersionUsesQpack(session->connection()->transport_version()));
  for (auto& it : session->stream_map()) {
    if (it.first == QuicUtils::GetHeadersStreamId(
                        session->connection()->transport_version())) {
      it.second.reset(headers_stream);
      session->headers_stream_ = static_cast<QuicHeadersStream*>(it.second.get());
      break;
    }
  }
}

// static
const spdy::SpdyFramer& QuicSpdySessionPeer::GetSpdyFramer(
    QuicSpdySession* session) {
  return session->spdy_framer_;
}

void QuicSpdySessionPeer::SetHpackEncoderDebugVisitor(
    QuicSpdySession* session,
    std::unique_ptr<QuicHpackDebugVisitor> visitor) {
  session->SetHpackEncoderDebugVisitor(std::move(visitor));
}

void QuicSpdySessionPeer::SetHpackDecoderDebugVisitor(
    QuicSpdySession* session,
    std::unique_ptr<QuicHpackDebugVisitor> visitor) {
  session->SetHpackDecoderDebugVisitor(std::move(visitor));
}

void QuicSpdySessionPeer::SetMaxInboundHeaderListSize(
    QuicSpdySession* session,
    size_t max_inbound_header_size) {
  session->set_max_inbound_header_list_size(max_inbound_header_size);
}

// static
size_t QuicSpdySessionPeer::WriteHeadersOnHeadersStream(
    QuicSpdySession* session,
    QuicStreamId id,
    spdy::SpdyHeaderBlock headers,
    bool fin,
    const spdy::SpdyStreamPrecedence& precedence,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  return session->WriteHeadersOnHeadersStream(
      id, std::move(headers), fin, precedence, std::move(ack_listener));
}

// static
QuicStreamId QuicSpdySessionPeer::GetNextOutgoingUnidirectionalStreamId(
    QuicSpdySession* session) {
  return session->GetNextOutgoingUnidirectionalStreamId();
}

// static
QuicReceiveControlStream* QuicSpdySessionPeer::GetReceiveControlStream(
    QuicSpdySession* session) {
  return session->receive_control_stream_;
}

// static
QuicSendControlStream* QuicSpdySessionPeer::GetSendControlStream(
    QuicSpdySession* session) {
  return session->send_control_stream_;
}

// static
QpackSendStream* QuicSpdySessionPeer::GetQpackDecoderSendStream(
    QuicSpdySession* session) {
  return session->qpack_decoder_send_stream_;
}

// static
QpackSendStream* QuicSpdySessionPeer::GetQpackEncoderSendStream(
    QuicSpdySession* session) {
  return session->qpack_encoder_send_stream_;
}

// static
QpackReceiveStream* QuicSpdySessionPeer::GetQpackDecoderReceiveStream(
    QuicSpdySession* session) {
  return session->qpack_decoder_receive_stream_;
}

// static
QpackReceiveStream* QuicSpdySessionPeer::GetQpackEncoderReceiveStream(
    QuicSpdySession* session) {
  return session->qpack_encoder_receive_stream_;
}

}  // namespace test
}  // namespace quic
