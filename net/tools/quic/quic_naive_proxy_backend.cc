// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "net/tools/quic/quic_naive_proxy_backend.h"

namespace net {

QuicNaiveProxyBackend::QuicNaiveProxyBackend() {}

QuicNaiveProxyBackend::~QuicNaiveProxyBackend() {}

bool QuicNaiveProxyBackend::InitializeBackend(const std::string& backend_url) {
  return true;
}

bool QuicNaiveProxyBackend::IsBackendInitialized() const {
  return true;
}

void QuicNaiveProxyBackend::FetchResponseFromBackend(
    const spdy::SpdyHeaderBlock& request_headers,
    const std::string& incoming_body,
    QuicSimpleServerBackend::RequestHandler* quic_server_stream) {
}

void QuicNaiveProxyBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* quic_server_stream) {
}

void QuicNaiveProxyBackend::OnReadHeaders(quic::QuicSpdyStream* stream,
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

void QuicNaiveProxyBackend::OnReadData(quic::QuicSpdyStream* stream,
    void* data, size_t len) {
  LOG(INFO) << "OnReadData " << stream;
}

void QuicNaiveProxyBackend::OnCloseStream(quic::QuicSpdyStream* stream) {
  LOG(INFO) << "OnCloseStream " << stream;
}

}  // namespace net
