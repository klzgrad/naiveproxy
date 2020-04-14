// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_BACKEND_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_BACKEND_H_

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/tools/quic_backend_response.h"

namespace spdy {
class SpdyHeaderBlock;
}  // namespace spdy

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
    // Called when the response is ready at the backend and can be send back to
    // the QUIC client.
    virtual void OnResponseBackendComplete(
        const QuicBackendResponse* response,
        std::list<QuicBackendResponse::ServerPushInfo> resources) = 0;
  };

  virtual ~QuicSimpleServerBackend() = default;
  // This method initializes the backend instance to fetch responses
  // from a backend server, in-memory cache etc.
  virtual bool InitializeBackend(const std::string& backend_url) = 0;
  // Returns true if the backend has been successfully initialized
  // and could be used to fetch HTTP requests
  virtual bool IsBackendInitialized() const = 0;
  // Triggers a HTTP request to be sent to the backend server or cache
  // If response is immediately available, the function synchronously calls
  // the |request_handler| with the HTTP response.
  // If the response has to be fetched over the network, the function
  // asynchronously calls |request_handler| with the HTTP response.
  virtual void FetchResponseFromBackend(
      const spdy::SpdyHeaderBlock& request_headers,
      const std::string& request_body,
      RequestHandler* request_handler) = 0;
  // Clears the state of the backend  instance
  virtual void CloseBackendResponseStream(RequestHandler* request_handler) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_BACKEND_H_
