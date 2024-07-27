// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_spdy_session_peer.h"

#include <utility>


#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/qpack/qpack_receive_stream.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {
namespace test {

// static
QuicHeadersStream* QuicSpdySessionPeer::GetHeadersStream(
    QuicSpdySession* session) {
  QUICHE_DCHECK(!VersionUsesHttp3(session->transport_version()));
  return session->headers_stream();
}

void QuicSpdySessionPeer::SetHeadersStream(QuicSpdySession* session,
                                           QuicHeadersStream* headers_stream) {
  QUICHE_DCHECK(!VersionUsesHttp3(session->transport_version()));
  for (auto& it : QuicSessionPeer::stream_map(session)) {
    if (it.first ==
        QuicUtils::GetHeadersStreamId(session->transport_version())) {
      it.second.reset(headers_stream);
      session->headers_stream_ = static_cast<QuicHeadersStream*>(it.second.get());
      break;
    }
  }
}

// static
spdy::SpdyFramer* QuicSpdySessionPeer::GetSpdyFramer(QuicSpdySession* session) {
  return &session->spdy_framer_;
}

void QuicSpdySessionPeer::SetMaxInboundHeaderListSize(
    QuicSpdySession* session, size_t max_inbound_header_size) {
  session->set_max_inbound_header_list_size(max_inbound_header_size);
}

// static
size_t QuicSpdySessionPeer::WriteHeadersOnHeadersStream(
    QuicSpdySession* session, QuicStreamId id, spdy::Http2HeaderBlock headers,
    bool fin, const spdy::SpdyStreamPrecedence& precedence,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
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

// static
void QuicSpdySessionPeer::SetHttpDatagramSupport(
    QuicSpdySession* session, HttpDatagramSupport http_datagram_support) {
  session->http_datagram_support_ = http_datagram_support;
}

// static
HttpDatagramSupport QuicSpdySessionPeer::LocalHttpDatagramSupport(
    QuicSpdySession* session) {
  return session->LocalHttpDatagramSupport();
}

// static
void QuicSpdySessionPeer::EnableWebTransport(QuicSpdySession* session) {
  QUICHE_DCHECK(session->WillNegotiateWebTransport());
  SetHttpDatagramSupport(session, HttpDatagramSupport::kRfc);
  session->peer_web_transport_versions_ = kDefaultSupportedWebTransportVersions;
}

}  // namespace test
}  // namespace quic
