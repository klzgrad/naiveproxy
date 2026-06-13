// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_server_initiated_spdy_stream.h"

#include "quiche/quic/core/quic_error_codes.h"

namespace quic {

void QuicServerInitiatedSpdyStream::OnBodyAvailable() {
  QUIC_BUG(Body received in QuicServerInitiatedSpdyStream)
      << "Received body data in QuicServerInitiatedSpdyStream.";
  OnUnrecoverableError(
      QUIC_INTERNAL_ERROR,
      "Received HTTP/3 body data in a server-initiated bidirectional stream");
}

size_t QuicServerInitiatedSpdyStream::WriteHeaders(
    quiche::HttpHeaderBlock /*header_block*/, bool /*fin*/,
    quiche::QuicheReferenceCountedPointer<
        QuicAckListenerInterface> /*ack_listener*/) {
  QUIC_BUG(Writing headers in QuicServerInitiatedSpdyStream)
      << "Attempting to write headers in QuicServerInitiatedSpdyStream";
  OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                       "Attempted to send HTTP/3 headers in a server-initiated "
                       "bidirectional stream");
  return 0;
}

void QuicServerInitiatedSpdyStream::OnInitialHeadersComplete(
    bool /*fin*/, size_t /*frame_len*/, const QuicHeaderList& /*header_list*/) {
  QUIC_PEER_BUG(Reading headers in QuicServerInitiatedSpdyStream)
      << "Attempting to receive headers in QuicServerInitiatedSpdyStream";

  OnUnrecoverableError(IETF_QUIC_PROTOCOL_VIOLATION,
                       "Received HTTP/3 headers in a server-initiated "
                       "bidirectional stream without an extension setting "
                       "explicitly allowing those");
}

}  // namespace quic
