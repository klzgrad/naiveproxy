// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/logging.h"
#include "net/third_party/quic/core/http/quic_header_list.h"
#include "net/third_party/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/spdy/core/spdy_header_block.h"
#include "net/tools/naive/quic_proxy_backend.h"

namespace net {

QuicProxyBackend::QuicProxyBackend(
    HttpNetworkSession* session,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : session_(session), traffic_annotation_(traffic_annotation) {}

QuicProxyBackend::~QuicProxyBackend() {}

bool QuicProxyBackend::InitializeBackend(const std::string& backend_url) {
  return true;
}

bool QuicProxyBackend::IsBackendInitialized() const {
  return true;
}

void QuicProxyBackend::FetchResponseFromBackend(
    const spdy::SpdyHeaderBlock& request_headers,
    const std::string& incoming_body,
    QuicSimpleServerBackend::RequestHandler* quic_stream) {}

void QuicProxyBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* quic_stream) {}

void QuicProxyBackend::OnReadHeaders(quic::QuicSpdyStream* stream,
                                     const quic::QuicHeaderList& header_list) {
  for (const auto& p : header_list) {
    const auto& name = p.first;
    const auto& value = p.second;
    if (name == ":method" && value != "CONNECT") {
      spdy::SpdyHeaderBlock headers;
      headers[":status"] = "405";
      stream->WriteHeaders(std::move(headers), /*fin=*/true, nullptr);
      return;
    }
    if (name == ":authority") {
    }
  }

  LOG(INFO) << "OnReadHeaders " << stream;
}

void QuicProxyBackend::OnReadData(quic::QuicSpdyStream* stream,
                                  void* data,
                                  size_t len) {
  LOG(INFO) << "OnReadData " << stream;
}

void QuicProxyBackend::OnCloseStream(quic::QuicSpdyStream* stream) {
  LOG(INFO) << "OnCloseStream " << stream;
}

}  // namespace net
