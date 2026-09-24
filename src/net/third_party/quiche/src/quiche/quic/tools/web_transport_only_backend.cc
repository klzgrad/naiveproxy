// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/web_transport_only_backend.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/http/status_code_mapping.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

void WebTransportOnlyBackend::FetchResponseFromBackend(
    const quiche::HttpHeaderBlock&, const std::string&,
    RequestHandler* request_handler) {
  static QuicBackendResponse* response = []() {
    quiche::HttpHeaderBlock headers;
    headers[":status"] = "405";  // 405 Method Not Allowed
    headers["content-type"] = "text/plain";
    auto response = std::make_unique<QuicBackendResponse>();
    response->set_headers(std::move(headers));
    response->set_body("This endpoint only accepts WebTransport requests");
    return response.release();
  }();
  request_handler->OnResponseBackendComplete(response);
}

WebTransportOnlyBackend::WebTransportResponse
WebTransportOnlyBackend::ProcessWebTransportRequest(
    const quiche::HttpHeaderBlock& request_headers,
    webtransport::Session* session) {
  WebTransportResponse response;

  auto path = request_headers.find(":path");
  if (path == request_headers.end()) {
    response.response_headers[":status"] = "400";
    return response;
  }

  absl::StatusOr<std::unique_ptr<webtransport::SessionVisitor>> processed =
      callback_(path->second, session);
  if (!processed.ok()) {
    response.response_headers[":status"] =
        absl::StrCat(quiche::StatusToHttpStatusCode(processed.status()));
    return response;
  }
  response.response_headers[":status"] = "200";
  response.visitor = *std::move(processed);
  return response;
}

}  // namespace quic
