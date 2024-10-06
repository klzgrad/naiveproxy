// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_WEB_TRANSPORT_ONLY_BACKEND_H_
#define QUICHE_QUIC_TOOLS_WEB_TRANSPORT_ONLY_BACKEND_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// A callback to create a WebTransport session visitor for a given path and the
// session object. The path includes both the path and the query.
using WebTransportRequestCallback = quiche::MultiUseCallback<
    absl::StatusOr<std::unique_ptr<webtransport::SessionVisitor>>(
        absl::string_view path, WebTransportSession* session)>;

class WebTransportOnlyBackend : public QuicSimpleServerBackend {
 public:
  explicit WebTransportOnlyBackend(WebTransportRequestCallback callback)
      : callback_(std::move(callback)) {}

  // QuicSimpleServerBackend implementation.
  bool InitializeBackend(const std::string&) override { return true; }
  bool IsBackendInitialized() const override { return true; }
  void FetchResponseFromBackend(const quiche::HttpHeaderBlock&,
                                const std::string&,
                                RequestHandler* request_handler) override;
  void CloseBackendResponseStream(RequestHandler*) override {}
  bool SupportsWebTransport() override { return true; }
  WebTransportResponse ProcessWebTransportRequest(
      const quiche::HttpHeaderBlock& request_headers,
      WebTransportSession* session) override;

 private:
  WebTransportRequestCallback callback_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_WEB_TRANSPORT_ONLY_BACKEND_H_
