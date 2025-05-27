// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_test_backend.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/test_tools/web_transport_resets_backend.h"
#include "quiche/quic/tools/web_transport_test_visitors.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/web_transport/complete_buffer_visitor.h"
#include "quiche/web_transport/web_transport.h"
#include "quiche/web_transport/web_transport_headers.h"

namespace quic {
namespace test {

namespace {

// SessionCloseVisitor implements the "/session-close" endpoint.  If the client
// sends a unidirectional stream of format "code message" to this endpoint, it
// will close the session with the corresponding error code and error message.
// For instance, sending "42 test error" will cause it to be closed with code 42
// and message "test error".  As a special case, sending "DRAIN" would result in
// a DRAIN_WEBTRANSPORT_SESSION capsule being sent.
class SessionCloseVisitor : public WebTransportVisitor {
 public:
  SessionCloseVisitor(WebTransportSession* session) : session_(session) {}

  void OnSessionReady() override {}
  void OnSessionClosed(WebTransportSessionError /*error_code*/,
                       const std::string& /*error_message*/) override {}

  void OnIncomingBidirectionalStreamAvailable() override {}
  void OnIncomingUnidirectionalStreamAvailable() override {
    WebTransportStream* stream = session_->AcceptIncomingUnidirectionalStream();
    if (stream == nullptr) {
      return;
    }
    stream->SetVisitor(
        std::make_unique<WebTransportUnidirectionalEchoReadVisitor>(
            stream, [this](const std::string& data) {
              if (data == "DRAIN") {
                session_->NotifySessionDraining();
                return;
              }
              std::pair<absl::string_view, absl::string_view> parsed =
                  absl::StrSplit(data, absl::MaxSplits(' ', 1));
              WebTransportSessionError error_code = 0;
              bool success = absl::SimpleAtoi(parsed.first, &error_code);
              QUICHE_DCHECK(success) << data;
              session_->CloseSession(error_code, parsed.second);
            }));
    stream->visitor()->OnCanRead();
  }

  void OnDatagramReceived(absl::string_view /*datagram*/) override {}

  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

 private:
  WebTransportSession* session_;  // Not owned.
};

// SubprotocolStreamVisitor opens one stream that contains the selected
// subprotocol.
class SubprotocolStreamVisitor : public WebTransportVisitor {
 public:
  SubprotocolStreamVisitor(WebTransportSession* session) : session_(session) {}

  void OnSessionReady() override {
    OnCanCreateNewOutgoingUnidirectionalStream();
  }
  void OnSessionClosed(WebTransportSessionError /*error_code*/,
                       const std::string& /*error_message*/) override {}
  void OnIncomingBidirectionalStreamAvailable() override {}
  void OnIncomingUnidirectionalStreamAvailable() override {}
  void OnDatagramReceived(absl::string_view /*datagram*/) override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {
    if (sent_) {
      return;
    }
    webtransport::Stream* stream = session_->OpenOutgoingUnidirectionalStream();
    if (stream == nullptr) {
      return;
    }
    stream->SetVisitor(std::make_unique<webtransport::CompleteBufferVisitor>(
        stream, session_->GetNegotiatedSubprotocol().value_or("[none]")));
    stream->visitor()->OnCanWrite();
    sent_ = true;
  }

 private:
  WebTransportSession* session_;  // Not owned.
  bool sent_ = false;
};

}  // namespace

QuicSimpleServerBackend::WebTransportResponse
QuicTestBackend::ProcessWebTransportRequest(
    const quiche::HttpHeaderBlock& request_headers,
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
  // Match any "/echo.*" pass, e.g. "/echo_foobar"
  if (absl::StartsWith(path, "/echo")) {
    WebTransportResponse response;
    response.response_headers[":status"] = "200";
    // Add response headers if the paramer has "set-header=XXX:YYY" query.
    GURL url = GURL(absl::StrCat("https://localhost", path));
    const std::vector<std::string>& params = absl::StrSplit(url.query(), '&');
    for (const auto& param : params) {
      absl::string_view param_view = param;
      if (absl::ConsumePrefix(&param_view, "set-header=")) {
        const std::vector<absl::string_view> header_value =
            absl::StrSplit(param_view, ':');
        if (header_value.size() == 2 &&
            !absl::StartsWith(header_value[0], ":")) {
          response.response_headers[header_value[0]] = header_value[1];
        }
      }
    }

    response.visitor =
        std::make_unique<EchoWebTransportSessionVisitor>(session);
    return response;
  }
  if (path == "/resets") {
    return WebTransportResetsBackend(request_headers, session);
  }
  if (path == "/session-close") {
    WebTransportResponse response;
    response.response_headers[":status"] = "200";
    response.visitor = std::make_unique<SessionCloseVisitor>(session);
    return response;
  }
  if (path == "/selected-subprotocol") {
    auto subprotocol_it =
        request_headers.find(webtransport::kSubprotocolRequestHeader);
    if (subprotocol_it == request_headers.end()) {
      WebTransportResponse response;
      response.response_headers[":status"] = "400";
      return response;
    }
    absl::StatusOr<std::vector<std::string>> subprotocols =
        webtransport::ParseSubprotocolRequestHeader(subprotocol_it->second);
    if (!subprotocols.ok() || subprotocols->empty()) {
      WebTransportResponse response;
      response.response_headers[":status"] = "400";
      return response;
    }
    size_t subprotocol_index = 0;
    auto subprotocol_index_it = request_headers.find("subprotocol-index");
    if (subprotocol_index_it != request_headers.end()) {
      if (!absl::SimpleAtoi(subprotocol_index_it->second, &subprotocol_index) ||
          subprotocol_index >= subprotocols->size()) {
        WebTransportResponse response;
        response.response_headers[":status"] = "400";
        return response;
      }
    }
    WebTransportResponse response;
    response.response_headers[":status"] = "200";
    response.response_headers[webtransport::kSubprotocolResponseHeader] =
        (*subprotocols)[subprotocol_index];
    response.visitor = std::make_unique<SubprotocolStreamVisitor>(session);
    return response;
  }

  WebTransportResponse response;
  response.response_headers[":status"] = "404";
  return response;
}

}  // namespace test
}  // namespace quic
