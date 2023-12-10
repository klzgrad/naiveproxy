// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/devious_baton.h"
#include "quiche/quic/tools/quic_server.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/quic/tools/web_transport_test_visitors.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_default_proof_providers.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/quiche_random.h"
#include "quiche/web_transport/web_transport.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    int32_t, port, 6121, "The port the WebTransport server will listen on.");

namespace quic {
namespace {

absl::StatusOr<std::unique_ptr<webtransport::SessionVisitor>> ProcessRequest(
    const GURL& url, WebTransportSession* session) {
  if (url.path() == "/webtransport/echo") {
    return std::make_unique<EchoWebTransportSessionVisitor>(session);
  }
  if (url.path() == "/webtransport/devious-baton") {
    int count = 1;
    DeviousBatonValue initial_value =
        quiche::QuicheRandom::GetInstance()->RandUint64() % 256;
    std::string query = url.query();
    url::Component query_component, key_component, value_component;
    query_component.begin = 0;
    query_component.len = query.size();
    while (url::ExtractQueryKeyValue(query.data(), &query_component,
                                     &key_component, &value_component)) {
      absl::string_view key(query.data() + key_component.begin,
                            key_component.len);
      absl::string_view value(query.data() + value_component.begin,
                              value_component.len);
      int parsed_value;
      if (!absl::SimpleAtoi(value, &parsed_value) || parsed_value < 0 ||
          parsed_value > 255) {
        if (key == "count" || key == "baton") {
          return absl::InvalidArgumentError(
              absl::StrCat("Failed to parse query param ", key));
        }
        continue;
      }
      if (key == "count") {
        count = parsed_value;
      }
      if (key == "baton") {
        initial_value = parsed_value;
      }
    }
    return std::make_unique<DeviousBatonSessionVisitor>(
        session, /*is_server=*/true, initial_value, count);
  }
  return absl::NotFoundError("Path not found");
}

class WebTransportTestBackend : public QuicSimpleServerBackend {
 public:
  bool InitializeBackend(const std::string&) override { return true; }
  bool IsBackendInitialized() const override { return true; }
  void FetchResponseFromBackend(const spdy::Http2HeaderBlock&,
                                const std::string&,
                                RequestHandler* request_handler) override {
    request_handler->TerminateStreamWithError(
        QuicResetStreamError::FromInternal(QUIC_STREAM_INTERNAL_ERROR));
  }
  void CloseBackendResponseStream(RequestHandler*) override {}
  bool SupportsWebTransport() override { return true; }
  WebTransportResponse ProcessWebTransportRequest(
      const spdy::Http2HeaderBlock& request_headers,
      WebTransportSession* session) override {
    WebTransportResponse response;
    response.response_headers[":status"] = "400";

    auto path = request_headers.find(":path");
    if (path == request_headers.end()) {
      return response;
    }
    GURL url(absl::StrCat("https://localhost", path->second));
    if (!url.is_valid()) {
      return response;
    }
    absl::StatusOr<std::unique_ptr<webtransport::SessionVisitor>> processed =
        ProcessRequest(url, session);
    switch (processed.status().code()) {
      case absl::StatusCode::kOk:
        response.response_headers[":status"] = "200";
        response.visitor = *std::move(processed);
        return response;
      case absl::StatusCode::kNotFound:
        response.response_headers[":status"] = "404";
        return response;
      case absl::StatusCode::kInvalidArgument:
        response.response_headers[":status"] = "400";
        return response;
      default:
        response.response_headers[":status"] = "500";
        return response;
    }
  }
};

int Main(int argc, char** argv) {
  quiche::QuicheSystemEventLoop event_loop("web_transport_test_server");
  const char* usage = "Usage: web_transport_test_server [options]";
  std::vector<std::string> non_option_args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  WebTransportTestBackend backend;
  QuicServer server(quiche::CreateDefaultProofSource(), &backend);
  quic::QuicSocketAddress addr(quic::QuicIpAddress::Any6(),
                               quiche::GetQuicheCommandLineFlag(FLAGS_port));
  if (!server.CreateUDPSocketAndListen(addr)) {
    QUICHE_LOG(ERROR) << "Failed to bind the port address";
  }
  QUICHE_LOG(INFO) << "Bound the server on " << addr;
  server.HandleEventsForever();
  return 0;
}

}  // namespace
}  // namespace quic

int main(int argc, char** argv) { return quic::Main(argc, argv); }
