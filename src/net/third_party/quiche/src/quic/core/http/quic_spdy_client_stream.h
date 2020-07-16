// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_STREAM_H_

#include <cstddef>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

namespace quic {

class QuicSpdyClientSession;

// All this does right now is send an SPDY request, and aggregate the
// SPDY response.
class QUIC_NO_EXPORT QuicSpdyClientStream : public QuicSpdyStream {
 public:
  QuicSpdyClientStream(QuicStreamId id,
                       QuicSpdyClientSession* session,
                       StreamType type);
  QuicSpdyClientStream(PendingStream* pending,
                       QuicSpdyClientSession* spdy_session,
                       StreamType type);
  QuicSpdyClientStream(const QuicSpdyClientStream&) = delete;
  QuicSpdyClientStream& operator=(const QuicSpdyClientStream&) = delete;
  ~QuicSpdyClientStream() override;

  // Override the base class to parse and store headers.
  void OnInitialHeadersComplete(bool fin,
                                size_t frame_len,
                                const QuicHeaderList& header_list) override;

  // Override the base class to parse and store trailers.
  void OnTrailingHeadersComplete(bool fin,
                                 size_t frame_len,
                                 const QuicHeaderList& header_list) override;

  // Override the base class to handle creation of the push stream.
  void OnPromiseHeaderList(QuicStreamId promised_id,
                           size_t frame_len,
                           const QuicHeaderList& header_list) override;

  // QuicStream implementation called by the session when there's data for us.
  void OnBodyAvailable() override;

  // Serializes the headers and body, sends it to the server, and
  // returns the number of bytes sent.
  size_t SendRequest(spdy::SpdyHeaderBlock headers,
                     quiche::QuicheStringPiece body,
                     bool fin);

  // Returns the response data.
  const std::string& data() { return data_; }

  // Returns whatever headers have been received for this stream.
  const spdy::SpdyHeaderBlock& response_headers() { return response_headers_; }

  const spdy::SpdyHeaderBlock& preliminary_headers() {
    return preliminary_headers_;
  }

  size_t header_bytes_read() const { return header_bytes_read_; }

  size_t header_bytes_written() const { return header_bytes_written_; }

  int response_code() const { return response_code_; }

  // While the server's SetPriority shouldn't be called externally, the creator
  // of client-side streams should be able to set the priority.
  using QuicSpdyStream::SetPriority;

 private:
  // The parsed headers received from the server.
  spdy::SpdyHeaderBlock response_headers_;

  // The parsed content-length, or -1 if none is specified.
  int64_t content_length_;
  int response_code_;
  std::string data_;
  size_t header_bytes_read_;
  size_t header_bytes_written_;

  QuicSpdyClientSession* session_;

  // These preliminary headers are used for the 100 Continue headers
  // that may arrive before the response headers when the request has
  // Expect: 100-continue.
  bool has_preliminary_headers_;
  spdy::SpdyHeaderBlock preliminary_headers_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_STREAM_H_
