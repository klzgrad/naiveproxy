// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// // Use of this source code is governed by a BSD-style license that can be
// // found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TOOLS_QUIC_NAIVE_SERVER_STREAM_H_
#define NET_THIRD_PARTY_QUIC_TOOLS_QUIC_NAIVE_SERVER_STREAM_H_

#include "base/macros.h"
#include "net/third_party/quic/core/http/quic_spdy_server_stream_base.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/tools/quic_backend_response.h"
#include "net/third_party/quic/tools/quic_simple_server_backend.h"
#include "net/third_party/spdy/core/spdy_framer.h"

namespace quic {

// All this does right now is aggregate data, and on fin, send an HTTP
// response.
class QuicNaiveServerStream : public QuicSpdyServerStreamBase,
                               public QuicSimpleServerBackend::RequestHandler {
 public:
  QuicNaiveServerStream(QuicStreamId id,
                        QuicSpdySession* session,
                        QuicSimpleServerBackend* backend);
  QuicNaiveServerStream(const QuicNaiveServerStream&) = delete;
  QuicNaiveServerStream& operator=(const QuicNaiveServerStream&) = delete;
  ~QuicNaiveServerStream() override;

  void SendErrorResponse(int resp_code);

  // QuicSpdyStream
  void OnInitialHeadersComplete(bool fin,
                                size_t frame_len,
                                const QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin,
                                 size_t frame_len,
                                 const QuicHeaderList& header_list) override;

  // QuicStream implementation called by the sequencer when there is
  // data (or a FIN) to be read.
  void OnDataAvailable() override;

  virtual void PushResponse(spdy::SpdyHeaderBlock push_request_headers);

  // Implements QuicSimpleServerBackend::RequestHandler callbacks
  QuicConnectionId connection_id() const override;
  QuicStreamId stream_id() const override;
  QuicString peer_host() const override;
  void OnResponseBackendComplete(
      const QuicBackendResponse* response,
      std::list<QuicBackendResponse::ServerPushInfo> resources) override;

 private:
  QuicSimpleServerBackend* backend_;  // Not owned.
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TOOLS_QUIC_NAIVE_SERVER_STREAM_H_
