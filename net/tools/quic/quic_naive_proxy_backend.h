// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_NAIVE_PROXY_BACKEND_H_
#define NET_TOOLS_QUIC_QUIC_NAIVE_PROXY_BACKEND_H_

#include <cstddef>

#include "base/callback.h"
#include "base/macros.h"
#include "net/third_party/quic/tools/quic_simple_server_backend.h"

namespace quic {
class QuicSpdyStream;
}  // namespace quic

namespace net {

// Manages the context to proxy HTTP requests to the backend server
// Owns instance of net::URLRequestContext.
class QuicNaiveProxyBackend : public quic::QuicSimpleServerBackend {
 public:
  explicit QuicNaiveProxyBackend();
  ~QuicNaiveProxyBackend() override;

  // Implements the functions for interface quic::QuicSimpleServerBackend
  bool InitializeBackend(const std::string& backend_url) override;
  bool IsBackendInitialized() const override;
  void FetchResponseFromBackend(
      const spdy::SpdyHeaderBlock& request_headers,
      const std::string& incoming_body,
      quic::QuicSimpleServerBackend::RequestHandler* quic_stream) override;
  void CloseBackendResponseStream(
      quic::QuicSimpleServerBackend::RequestHandler* quic_stream) override;

  void OnReadHeaders(quic::QuicSpdyStream* stream,
      const quic::QuicHeaderList& header_list) override;
  void OnReadData(quic::QuicSpdyStream* stream,
      void* data, size_t len) override;
  void OnCloseStream(quic::QuicSpdyStream* stream) override;

 private:
  // Maps quic streams in the frontend to the corresponding http streams
  // managed by |this|
  //ProxyBackendStreamMap backend_stream_map_;

  DISALLOW_COPY_AND_ASSIGN(QuicNaiveProxyBackend);
};
}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_NAIVE_PROXY_BACKEND_H_
