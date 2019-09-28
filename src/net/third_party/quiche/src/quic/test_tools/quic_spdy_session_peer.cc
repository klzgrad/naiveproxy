// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {
namespace test {

// static
QuicHeadersStream* QuicSpdySessionPeer::GetHeadersStream(
    QuicSpdySession* session) {
  return session->headers_stream();
}

void QuicSpdySessionPeer::SetHeadersStream(QuicSpdySession* session,
                                           QuicHeadersStream* headers_stream) {
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
    spdy::SpdyPriority priority,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  return session->WriteHeadersOnHeadersStream(
      id, std::move(headers), fin, priority, std::move(ack_listener));
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

}  // namespace test
}  // namespace quic
