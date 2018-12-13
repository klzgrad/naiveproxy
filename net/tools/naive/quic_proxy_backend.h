// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_QUIC_PROXY_BACKEND_H_
#define NET_TOOLS_NAIVE_QUIC_PROXY_BACKEND_H_

#include <cstddef>
#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "net/third_party/quic/tools/quic_simple_server_backend.h"

namespace spdy {
class SpdyHeaderBlock;
}  // namespace spdy

namespace quic {
class QuicSpdyStream;
class QuicHeaderList;
}  // namespace quic

namespace net {
class HttpNetworkSession;
struct NetworkTrafficAnnotationTag;

class QuicProxyBackend : public quic::QuicSimpleServerBackend {
 public:
  QuicProxyBackend(HttpNetworkSession* session,
                   const NetworkTrafficAnnotationTag& traffic_annotation);
  ~QuicProxyBackend() override;

  // Implements quic::QuicSimpleServerBackend
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
                  void* data,
                  size_t len) override;
  void OnCloseStream(quic::QuicSpdyStream* stream) override;

 private:
  HttpNetworkSession* session_;
  const NetworkTrafficAnnotationTag& traffic_annotation_;

  DISALLOW_COPY_AND_ASSIGN(QuicProxyBackend);
};
}  // namespace net

#endif  // NET_TOOLS_NAIVE_QUIC_PROXY_BACKEND_H_
