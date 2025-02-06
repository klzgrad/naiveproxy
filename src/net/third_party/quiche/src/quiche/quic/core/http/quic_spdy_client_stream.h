// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_STREAM_H_

#include <cstddef>
#include <list>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/spdy_framer.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

class QuicSpdyClientSession;

// All this does right now is send an SPDY request, and aggregate the
// SPDY response.
class QUICHE_EXPORT QuicSpdyClientStream : public QuicSpdyStream {
 public:
  QuicSpdyClientStream(QuicStreamId id, QuicSpdyClientSession* session,
                       StreamType type);
  QuicSpdyClientStream(PendingStream* pending,
                       QuicSpdyClientSession* spdy_session);
  QuicSpdyClientStream(const QuicSpdyClientStream&) = delete;
  QuicSpdyClientStream& operator=(const QuicSpdyClientStream&) = delete;
  ~QuicSpdyClientStream() override;

  // Override the base class to parse and store headers.
  void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                const QuicHeaderList& header_list) override;

  // Override the base class to parse and store trailers.
  void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                 const QuicHeaderList& header_list) override;

  // QuicStream implementation called by the session when there's data for us.
  void OnBodyAvailable() override;

  void OnFinRead() override;

  // Serializes the headers and body, sends it to the server, and
  // returns the number of bytes sent.
  size_t SendRequest(quiche::HttpHeaderBlock headers, absl::string_view body,
                     bool fin);

  // Returns the response data.
  absl::string_view data() const { return data_; }

  // Returns whatever headers have been received for this stream.
  const quiche::HttpHeaderBlock& response_headers() {
    return response_headers_;
  }

  const std::list<quiche::HttpHeaderBlock>& preliminary_headers() {
    return preliminary_headers_;
  }

  size_t header_bytes_read() const { return header_bytes_read_; }

  size_t header_bytes_written() const { return header_bytes_written_; }

  int response_code() const { return response_code_; }

  QuicTime::Delta time_to_response_headers_received() const {
    return time_to_response_headers_received_;
  }

  QuicTime::Delta time_to_response_complete() const {
    return time_to_response_complete_;
  }

  // While the server's SetPriority shouldn't be called externally, the creator
  // of client-side streams should be able to set the priority.
  using QuicSpdyStream::SetPriority;

 protected:
  bool ValidateReceivedHeaders(const QuicHeaderList& header_list) override;

  // Called by OnInitialHeadersComplete to set response_header_. Returns false
  // on error.
  virtual bool CopyAndValidateHeaders(const QuicHeaderList& header_list,
                                      int64_t& content_length,
                                      quiche::HttpHeaderBlock& headers);

  // Called by OnInitialHeadersComplete to set response_code_ based on
  // response_header_. Returns false on error.
  virtual bool ParseAndValidateStatusCode();

  bool uses_capsules() const override {
    return QuicSpdyStream::uses_capsules() && !capsules_failed_;
  }

 private:
  // The parsed headers received from the server.
  quiche::HttpHeaderBlock response_headers_;

  // The parsed content-length, or -1 if none is specified.
  int64_t content_length_;
  int response_code_;
  bool capsules_failed_ = false;
  std::string data_;
  size_t header_bytes_read_;
  size_t header_bytes_written_;

  QuicSpdyClientSession* session_;

  // These preliminary headers are used for interim response headers that may
  // arrive before the final response headers.
  std::list<quiche::HttpHeaderBlock> preliminary_headers_;

  QuicTime::Delta time_to_response_headers_received_ =
      QuicTime::Delta::Infinite();
  QuicTime::Delta time_to_response_complete_ = QuicTime::Delta::Infinite();
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_STREAM_H_
