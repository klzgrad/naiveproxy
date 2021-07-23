// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/test_tools/quic_test_backend.h"

#include <cstring>
#include <memory>

#include "absl/strings/string_view.h"
#include "quic/core/quic_buffer_allocator.h"
#include "quic/core/quic_simple_buffer_allocator.h"
#include "quic/core/web_transport_interface.h"
#include "quic/platform/api/quic_mem_slice.h"
#include "quic/tools/web_transport_test_visitors.h"

namespace quic {
namespace test {

QuicSimpleServerBackend::WebTransportResponse
QuicTestBackend::ProcessWebTransportRequest(
    const spdy::Http2HeaderBlock& request_headers,
    WebTransportSession* session) {
  if (!SupportsWebTransport()) {
    return QuicSimpleServerBackend::ProcessWebTransportRequest(request_headers,
                                                               session);
  }

  auto path_it = request_headers.find(":path");
  if (path_it == request_headers.end()) {
    WebTransportResponse response;
    response.response_headers[":status"] = "400";
    return response;
  }
  absl::string_view path = path_it->second;
  if (path == "/echo") {
    WebTransportResponse response;
    response.response_headers[":status"] = "200";
    response.visitor =
        std::make_unique<EchoWebTransportSessionVisitor>(session);
    return response;
  }

  WebTransportResponse response;
  response.response_headers[":status"] = "404";
  return response;
}

}  // namespace test
}  // namespace quic
