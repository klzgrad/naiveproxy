// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// // Use of this source code is governed by a BSD-style license that can be
// // found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SIMPLE_SERVER_STREAM_H_
#define NET_TOOLS_QUIC_QUIC_SIMPLE_SERVER_STREAM_H_

#include <string>

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_spdy_stream.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/spdy/core/spdy_framer.h"
#include "net/tools/quic/quic_http_response_cache.h"
#include "net/tools/quic/quic_spdy_server_stream_base.h"

namespace net {

namespace test {
class QuicSimpleServerStreamPeer;
}  // namespace test

// All this does right now is aggregate data, and on fin, send an HTTP
// response.
class QuicSimpleServerStream : public QuicSpdyServerStreamBase {
 public:
  QuicSimpleServerStream(QuicStreamId id,
                         QuicSpdySession* session,
                         QuicHttpResponseCache* response_cache);
  ~QuicSimpleServerStream() override;

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

  // Make this stream start from as if it just finished parsing an incoming
  // request whose headers are equivalent to |push_request_headers|.
  // Doing so will trigger this toy stream to fetch response and send it back.
  virtual void PushResponse(SpdyHeaderBlock push_request_headers);

  // The response body of error responses.
  static const char* const kErrorResponseBody;
  static const char* const kNotFoundResponseBody;

 protected:
  // Sends a basic 200 response using SendHeaders for the headers and WriteData
  // for the body.
  virtual void SendResponse();

  // Sends a basic 500 response using SendHeaders for the headers and WriteData
  // for the body.
  virtual void SendErrorResponse();

  // Sends a basic 404 response using SendHeaders for the headers and WriteData
  // for the body.
  void SendNotFoundResponse();

  void SendHeadersAndBody(SpdyHeaderBlock response_headers,
                          QuicStringPiece body);
  void SendHeadersAndBodyAndTrailers(SpdyHeaderBlock response_headers,
                                     QuicStringPiece body,
                                     SpdyHeaderBlock response_trailers);

  SpdyHeaderBlock* request_headers() { return &request_headers_; }

  const std::string& body() { return body_; }

 private:
  friend class test::QuicSimpleServerStreamPeer;

  // The parsed headers received from the client.
  SpdyHeaderBlock request_headers_;
  int64_t content_length_;
  std::string body_;

  QuicHttpResponseCache* response_cache_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(QuicSimpleServerStream);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SIMPLE_SERVER_STREAM_H_
