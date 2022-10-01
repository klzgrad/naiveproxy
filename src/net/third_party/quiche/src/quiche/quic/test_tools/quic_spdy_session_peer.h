// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_

#include "quiche/quic/core/http/quic_receive_control_stream.h"
#include "quiche/quic/core/http/quic_send_control_stream.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/qpack/qpack_receive_stream.h"
#include "quiche/quic/core/qpack/qpack_send_stream.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_write_blocked_list.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_framer.h"

namespace quic {

class QuicHeadersStream;

namespace test {

class QuicSpdySessionPeer {
 public:
  QuicSpdySessionPeer() = delete;

  static QuicHeadersStream* GetHeadersStream(QuicSpdySession* session);
  static void SetHeadersStream(QuicSpdySession* session,
                               QuicHeadersStream* headers_stream);
  static spdy::SpdyFramer* GetSpdyFramer(QuicSpdySession* session);
  // Must be called before Initialize().
  static void SetMaxInboundHeaderListSize(QuicSpdySession* session,
                                          size_t max_inbound_header_size);
  static size_t WriteHeadersOnHeadersStream(
      QuicSpdySession* session, QuicStreamId id, spdy::Http2HeaderBlock headers,
      bool fin, const spdy::SpdyStreamPrecedence& precedence,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);
  // |session| can't be nullptr.
  static QuicStreamId GetNextOutgoingUnidirectionalStreamId(
      QuicSpdySession* session);
  static QuicReceiveControlStream* GetReceiveControlStream(
      QuicSpdySession* session);
  static QuicSendControlStream* GetSendControlStream(QuicSpdySession* session);
  static QpackSendStream* GetQpackDecoderSendStream(QuicSpdySession* session);
  static QpackSendStream* GetQpackEncoderSendStream(QuicSpdySession* session);
  static QpackReceiveStream* GetQpackDecoderReceiveStream(
      QuicSpdySession* session);
  static QpackReceiveStream* GetQpackEncoderReceiveStream(
      QuicSpdySession* session);
  static void SetHttpDatagramSupport(QuicSpdySession* session,
                                     HttpDatagramSupport http_datagram_support);
  static HttpDatagramSupport LocalHttpDatagramSupport(QuicSpdySession* session);
  static void EnableWebTransport(QuicSpdySession* session);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
