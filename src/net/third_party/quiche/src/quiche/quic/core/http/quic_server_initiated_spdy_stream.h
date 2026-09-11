// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SERVER_INITIATED_SPDY_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SERVER_INITIATED_SPDY_STREAM_H_

#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

// QuicServerInitiatedSpdyStream is a subclass of QuicSpdyStream meant to handle
// WebTransport traffic on server-initiated bidirectional streams.  Receiving or
// sending any other traffic on this stream will result in a CONNECTION_CLOSE.
class QUICHE_EXPORT QuicServerInitiatedSpdyStream : public QuicSpdyStream {
 public:
  using QuicSpdyStream::QuicSpdyStream;

  void OnBodyAvailable() override;
  size_t WriteHeaders(
      quiche::HttpHeaderBlock header_block, bool fin,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener) override;
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const QuicHeaderList& header_list) override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SERVER_INITIATED_SPDY_STREAM_H_
