// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_STREAM_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_server_stream_base.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/tools/quic_backend_response.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_backend.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

namespace quic {

// All this does right now is aggregate data, and on fin, send an HTTP
// response.
class QuicSimpleServerStream : public QuicSpdyServerStreamBase,
                               public QuicSimpleServerBackend::RequestHandler {
 public:
  QuicSimpleServerStream(QuicStreamId id,
                         QuicSpdySession* session,
                         StreamType type,
                         QuicSimpleServerBackend* quic_simple_server_backend);
  QuicSimpleServerStream(PendingStream* pending,
                         QuicSpdySession* session,
                         StreamType type,
                         QuicSimpleServerBackend* quic_simple_server_backend);
  QuicSimpleServerStream(const QuicSimpleServerStream&) = delete;
  QuicSimpleServerStream& operator=(const QuicSimpleServerStream&) = delete;
  ~QuicSimpleServerStream() override;

  // QuicSpdyStream
  void OnInitialHeadersComplete(bool fin,
                                size_t frame_len,
                                const QuicHeaderList& header_list) override;
  void OnTrailingHeadersComplete(bool fin,
                                 size_t frame_len,
                                 const QuicHeaderList& header_list) override;
  void OnCanWrite() override;

  // QuicStream implementation called by the sequencer when there is
  // data (or a FIN) to be read.
  void OnBodyAvailable() override;

  // Make this stream start from as if it just finished parsing an incoming
  // request whose headers are equivalent to |push_request_headers|.
  // Doing so will trigger this toy stream to fetch response and send it back.
  virtual void PushResponse(spdy::SpdyHeaderBlock push_request_headers);

  // The response body of error responses.
  static const char* const kErrorResponseBody;
  static const char* const kNotFoundResponseBody;

  // Implements QuicSimpleServerBackend::RequestHandler callbacks
  QuicConnectionId connection_id() const override;
  QuicStreamId stream_id() const override;
  std::string peer_host() const override;
  void OnResponseBackendComplete(
      const QuicBackendResponse* response,
      std::list<QuicBackendResponse::ServerPushInfo> resources) override;

 protected:
  // Sends a basic 200 response using SendHeaders for the headers and WriteData
  // for the body.
  virtual void SendResponse();

  // Sends a basic 500 response using SendHeaders for the headers and WriteData
  // for the body.
  virtual void SendErrorResponse();
  void SendErrorResponse(int resp_code);

  // Sends a basic 404 response using SendHeaders for the headers and WriteData
  // for the body.
  void SendNotFoundResponse();

  // Sends the response header and body, but not the fin.
  void SendIncompleteResponse(spdy::SpdyHeaderBlock response_headers,
                              quiche::QuicheStringPiece body);

  void SendHeadersAndBody(spdy::SpdyHeaderBlock response_headers,
                          quiche::QuicheStringPiece body);
  void SendHeadersAndBodyAndTrailers(spdy::SpdyHeaderBlock response_headers,
                                     quiche::QuicheStringPiece body,
                                     spdy::SpdyHeaderBlock response_trailers);

  spdy::SpdyHeaderBlock* request_headers() { return &request_headers_; }

  const std::string& body() { return body_; }

  // Writes the body bytes for the GENERATE_BYTES response type.
  void WriteGeneratedBytes();

  // The parsed headers received from the client.
  spdy::SpdyHeaderBlock request_headers_;
  int64_t content_length_;
  std::string body_;

 private:
  uint64_t generate_bytes_length_;

  QuicSimpleServerBackend* quic_simple_server_backend_;  // Not owned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_STREAM_H_
