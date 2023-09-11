// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_BACKEND_RESPONSE_H_
#define QUICHE_QUIC_TOOLS_QUIC_BACKEND_RESPONSE_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace quic {

// Container for HTTP response header/body pairs
// fetched by the QuicSimpleServerBackend
class QuicBackendResponse {
 public:
  // A ServerPushInfo contains path of the push request and everything needed in
  // comprising a response for the push request.
  // TODO(b/171463363): Remove.
  struct ServerPushInfo {
    ServerPushInfo(QuicUrl request_url, spdy::Http2HeaderBlock headers,
                   spdy::SpdyPriority priority, std::string body);
    ServerPushInfo(const ServerPushInfo& other);

    QuicUrl request_url;
    spdy::Http2HeaderBlock headers;
    spdy::SpdyPriority priority;
    std::string body;
  };

  enum SpecialResponseType {
    REGULAR_RESPONSE,      // Send the headers and body like a server should.
    CLOSE_CONNECTION,      // Close the connection (sending the close packet).
    IGNORE_REQUEST,        // Do nothing, expect the client to time out.
    BACKEND_ERR_RESPONSE,  // There was an error fetching the response from
                           // the backend, for example as a TCP connection
                           // error.
    INCOMPLETE_RESPONSE,   // The server will act as if there is a non-empty
                           // trailer but it will not be sent, as a result, FIN
                           // will not be sent too.
    GENERATE_BYTES         // Sends a response with a length equal to the number
                           // of bytes in the URL path.
  };
  QuicBackendResponse();

  QuicBackendResponse(const QuicBackendResponse& other) = delete;
  QuicBackendResponse& operator=(const QuicBackendResponse& other) = delete;

  ~QuicBackendResponse();

  const std::vector<spdy::Http2HeaderBlock>& early_hints() const {
    return early_hints_;
  }
  SpecialResponseType response_type() const { return response_type_; }
  const spdy::Http2HeaderBlock& headers() const { return headers_; }
  const spdy::Http2HeaderBlock& trailers() const { return trailers_; }
  const absl::string_view body() const { return absl::string_view(body_); }

  void AddEarlyHints(const spdy::Http2HeaderBlock& headers) {
    spdy::Http2HeaderBlock hints = headers.Clone();
    hints[":status"] = "103";
    early_hints_.push_back(std::move(hints));
  }

  void set_response_type(SpecialResponseType response_type) {
    response_type_ = response_type;
  }

  void set_headers(spdy::Http2HeaderBlock headers) {
    headers_ = std::move(headers);
  }
  void set_trailers(spdy::Http2HeaderBlock trailers) {
    trailers_ = std::move(trailers);
  }
  void set_body(absl::string_view body) {
    body_.assign(body.data(), body.size());
  }

  // This would simulate a delay before sending the response
  // back to the client. Intended for testing purposes.
  void set_delay(QuicTime::Delta delay) { delay_ = delay; }
  QuicTime::Delta delay() const { return delay_; }

 private:
  std::vector<spdy::Http2HeaderBlock> early_hints_;
  SpecialResponseType response_type_;
  spdy::Http2HeaderBlock headers_;
  spdy::Http2HeaderBlock trailers_;
  std::string body_;
  QuicTime::Delta delay_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_BACKEND_RESPONSE_H_
