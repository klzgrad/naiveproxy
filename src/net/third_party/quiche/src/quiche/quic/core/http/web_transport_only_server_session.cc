// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/web_transport_only_server_session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/http/quic_server_initiated_spdy_stream.h"
#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/http/status_code_mapping.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport_headers.h"

namespace quic {

namespace {
constexpr QuicStreamCount kDefaultMaxStreamsAcceptedPerLoop = 5;
}

WebTransportOnlyServerSession::~WebTransportOnlyServerSession() {
  DeleteConnection();
}

void WebTransportOnlyServerSession::Initialize() {
  QUICHE_DCHECK(handler_factory_ != nullptr);
  set_max_streams_accepted_per_loop(kDefaultMaxStreamsAcceptedPerLoop);
  QuicServerSessionBase::Initialize();
}

std::unique_ptr<QuicCryptoServerStreamBase>
WebTransportOnlyServerSession::CreateQuicCryptoServerStream(
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache) {
  return CreateCryptoServerStream(crypto_config, compressed_certs_cache, this,
                                  stream_helper());
}

QuicSpdyStream* WebTransportOnlyServerSession::CreateIncomingStream(
    QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }

  QuicSpdyStream* stream = new Stream(id, this, BIDIRECTIONAL);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

QuicSpdyStream* WebTransportOnlyServerSession::CreateIncomingStream(
    PendingStream* pending) {
  QuicSpdyStream* stream = new Stream(pending, this);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

QuicSpdyStream*
WebTransportOnlyServerSession::CreateOutgoingBidirectionalStream() {
  if (!ShouldCreateOutgoingBidirectionalStream()) {
    return nullptr;
  }

  QuicServerInitiatedSpdyStream* stream = new QuicServerInitiatedSpdyStream(
      GetNextOutgoingBidirectionalStreamId(), this, BIDIRECTIONAL);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

QuicStream* WebTransportOnlyServerSession::ProcessBidirectionalPendingStream(
    PendingStream* pending) {
  return CreateIncomingStream(pending);
}

bool WebTransportOnlyServerSession::OnSettingsFrame(
    const SettingsFrame& frame) {
  if (!QuicServerSessionBase::OnSettingsFrame(frame)) {
    return false;
  }
  if (!SupportsWebTransport()) {
    QUIC_DLOG(ERROR)
        << "Refusing connection that does not support WebTransport";
    return false;
  }
  return true;
}

void WebTransportOnlyServerSession::Stream::OnBodyAvailable() {
  QUIC_BUG(WebTransportOnlyServerSession_OnBodyAvailable)
      << "Received body on a WebTransportOnlyServerSession stream";
  OnUnrecoverableError(
      QUIC_INTERNAL_ERROR,
      "Received HTTP/3 body data within a WebTransport-only session");
}

void WebTransportOnlyServerSession::Stream::SendErrorResponse(int code) {
  if (!reading_stopped()) {
    StopReading();
  }
  quiche::HttpHeaderBlock headers;
  headers[":status"] = absl::StrCat(code);
  WriteHeaders(std::move(headers), /*fin=*/true, /*ack_listener=*/nullptr);
}

std::string WebTransportOnlyServerSession::Stream::SelectSubprotocolResponse(
    absl::string_view client_header_value) {
  // The specification requires that a malformed subprotocol header is
  // ignored, as is the failure to negotiate the value.
  absl::StatusOr<std::vector<std::string>> subprotocols_offered =
      webtransport::ParseSubprotocolRequestHeader(client_header_value);
  if (!subprotocols_offered.ok()) {
    return "";
  }

  // The callback requires a span of string_views instead of strings.
  absl::FixedArray<absl::string_view> subprotocol_views(
      subprotocols_offered->size());
  for (size_t i = 0; i < subprotocol_views.size(); ++i) {
    subprotocol_views[i] = (*subprotocols_offered)[i];
  }

  int selected = static_cast<WebTransportOnlyServerSession*>(session())
                     ->subprotocol_callback_(subprotocol_views);
  if (selected < 0 || selected >= subprotocol_views.size()) {
    return "";
  }

  return std::string(subprotocol_views[selected]);
}

void WebTransportOnlyServerSession::Stream::OnInitialHeadersComplete(
    bool fin, size_t frame_len, const QuicHeaderList& header_list) {
  QuicSpdyServerStreamBase::OnInitialHeadersComplete(fin, frame_len,
                                                     header_list);
  if (write_side_closed()) {
    return;
  }

  WebTransportIncomingRequestDetails details;
  int64_t content_length;
  if (!SpdyUtils::CopyAndValidateHeaders(header_list, &content_length,
                                         &details.headers)) {
    SendErrorResponse(400);
    return;
  }
  ConsumeHeaderList();

  if (web_transport() == nullptr) {
    // Return 405 Method Not Allowed if any of the QuicSpdySession-level checks
    // have prevented creation of the WebTransport session.
    SendErrorResponse(405);
    return;
  }

  auto subprotocol_request_it =
      details.headers.find(webtransport::kSubprotocolRequestHeader);
  if (subprotocol_request_it != details.headers.end()) {
    details.subprotocol =
        SelectSubprotocolResponse(subprotocol_request_it->second);
    // The specification requires that a malformed subprotocol header is
    // ignored.
  }

  WebTransportHandlerFactoryCallback& factory =
      static_cast<WebTransportOnlyServerSession*>(session())->handler_factory_;
  absl::StatusOr<WebTransportConnectResponse> response =
      factory(web_transport(), details);
  if (!response.ok()) {
    SendErrorResponse(quiche::StatusToHttpStatusCode(response.status()));
    return;
  }
  if (response->visitor == nullptr) {
    QUICHE_BUG(WebTransportOnlyServerSession_null_visitor)
        << "WebTransport request callback has returned a non-error response, "
           "but a null visitor.";
    SendErrorResponse(500);
    return;
  }

  response->headers[":status"] = "200";
  if (!details.subprotocol.empty()) {
    absl::StatusOr<std::string> serialized =
        webtransport::SerializeSubprotocolResponseHeader(details.subprotocol);
    if (serialized.ok()) {
      response->headers[webtransport::kSubprotocolResponseHeader] =
          *std::move(serialized);
    } else {
      QUIC_DLOG(WARNING) << "Response has invalid subprotocol listed: "
                         << details.subprotocol;
    }
  }

  WriteHeaders(std::move(response->headers), /*fin=*/false,
               /*ack_listener=*/nullptr);
  web_transport()->SetVisitor(std::move(response->visitor));
  // This should be the last call in the sequence, as it will trigger
  // OnSessionReady() and the related application logic.
  // TODO(vasilvv): add a test ensuring this is called.
  web_transport()->HeadersReceived(details.headers);
}

}  // namespace quic
