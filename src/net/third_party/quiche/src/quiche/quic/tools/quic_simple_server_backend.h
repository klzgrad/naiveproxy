// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_BACKEND_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_BACKEND_H_

#include <cstdint>
#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

// This interface implements the functionality to fetch a response
// from the backend (such as cache, http-proxy etc) to serve
// requests received by a Quic Server
class QuicSimpleServerBackend {
 public:
  // This interface implements the methods
  // called by the QuicSimpleServerBackend implementation
  // to process the request in the backend
  class RequestHandler {
   public:
    virtual ~RequestHandler() {}

    virtual QuicConnectionId connection_id() const = 0;
    virtual QuicStreamId stream_id() const = 0;
    virtual std::string peer_host() const = 0;
    virtual QuicSpdyStream* GetStream() = 0;
    // Called when the response is ready at the backend and can be send back to
    // the QUIC client.
    virtual void OnResponseBackendComplete(
        const QuicBackendResponse* response) = 0;
    // Sends additional non-full-response data (without headers) to the request
    // stream, e.g. for CONNECT data. May only be called after sending an
    // incomplete response (using `QuicBackendResponse::INCOMPLETE_RESPONSE`).
    // Sends the data with the FIN bit to close the stream if `close_stream` is
    // true.
    virtual void SendStreamData(absl::string_view data, bool close_stream) = 0;
    // Abruptly terminates (resets) the request stream with `error`.
    virtual void TerminateStreamWithError(QuicResetStreamError error) = 0;
  };

  struct WebTransportResponse {
    quiche::HttpHeaderBlock response_headers;
    std::unique_ptr<WebTransportVisitor> visitor;
  };

  virtual ~QuicSimpleServerBackend() = default;
  // This method initializes the backend instance to fetch responses
  // from a backend server, in-memory cache etc.
  virtual bool InitializeBackend(const std::string& backend_url) = 0;
  // Returns true if the backend has been successfully initialized
  // and could be used to fetch HTTP requests
  virtual bool IsBackendInitialized() const = 0;
  // Passes the socket factory in use by the QuicServer. Must live as long as
  // incoming requests/data are still sent to the backend, or until cleared by
  // calling with null. Must not be called while backend is handling requests.
  virtual void SetSocketFactory(SocketFactory* /*socket_factory*/) {}
  // Triggers a HTTP request to be sent to the backend server or cache
  // If response is immediately available, the function synchronously calls
  // the `request_handler` with the HTTP response.
  // If the response has to be fetched over the network, the function
  // asynchronously calls `request_handler` with the HTTP response.
  //
  // Not called for requests using the CONNECT method.
  virtual void FetchResponseFromBackend(
      const quiche::HttpHeaderBlock& request_headers,
      const std::string& request_body, RequestHandler* request_handler) = 0;

  // Handles headers for requests using the CONNECT method. Called immediately
  // on receiving the headers, potentially before the request is complete or
  // data is received. Any response (complete or incomplete) should be sent,
  // potentially asynchronously, using `request_handler`.
  //
  // If not overridden by backend, sends an error appropriate for a server that
  // does not handle CONNECT requests.
  virtual void HandleConnectHeaders(
      const quiche::HttpHeaderBlock& /*request_headers*/,
      RequestHandler* request_handler) {
    quiche::HttpHeaderBlock headers;
    headers[":status"] = "405";
    QuicBackendResponse response;
    response.set_headers(std::move(headers));
    request_handler->OnResponseBackendComplete(&response);
  }
  // Handles data for requests using the CONNECT method.  Called repeatedly
  // whenever new data is available. If `data_complete` is true, data was
  // received with the FIN bit, and this is the last call to this method.
  //
  // If not overridden by backend, abruptly terminates the stream.
  virtual void HandleConnectData(absl::string_view /*data*/,
                                 bool /*data_complete*/,
                                 RequestHandler* request_handler) {
    request_handler->TerminateStreamWithError(
        QuicResetStreamError::FromInternal(QUIC_STREAM_CONNECT_ERROR));
  }

  // Clears the state of the backend  instance
  virtual void CloseBackendResponseStream(RequestHandler* request_handler) = 0;

  virtual WebTransportResponse ProcessWebTransportRequest(
      const quiche::HttpHeaderBlock& /*request_headers*/,
      WebTransportSession* /*session*/) {
    WebTransportResponse response;
    response.response_headers[":status"] = "400";
    return response;
  }
  virtual bool SupportsWebTransport() { return false; }
  virtual bool SupportsExtendedConnect() { return true; }
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_BACKEND_H_
