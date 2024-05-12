// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/moqt_client.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/quic/tools/quic_event_loop_tools.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace moqt {

MoqtClient::MoqtClient(quic::QuicSocketAddress peer_address,
                       const quic::QuicServerId& server_id,
                       std::unique_ptr<quic::ProofVerifier> proof_verifier,
                       quic::QuicEventLoop* event_loop)
    : spdy_client_(peer_address, server_id, GetMoqtSupportedQuicVersions(),
                   event_loop, std::move(proof_verifier)) {
  spdy_client_.set_enable_web_transport(true);
}

void MoqtClient::Connect(std::string path, MoqtSessionCallbacks callbacks) {
  absl::Status status = ConnectInner(std::move(path), callbacks);
  if (!status.ok()) {
    std::move(callbacks.session_terminated_callback)(status.message());
  }
}

absl::Status MoqtClient::ConnectInner(std::string path,
                                      MoqtSessionCallbacks& callbacks) {
  if (!spdy_client_.Initialize()) {
    return absl::InternalError("Initialization failed");
  }
  if (!spdy_client_.Connect()) {
    return absl::UnavailableError("Failed to establish a QUIC connection");
  }
  bool settings_received = quic::ProcessEventsUntil(
      spdy_client_.default_network_helper()->event_loop(),
      [&] { return spdy_client_.client_session()->settings_received(); });
  if (!settings_received) {
    return absl::UnavailableError(
        "Timed out while waiting for server SETTINGS");
  }
  if (!spdy_client_.client_session()->SupportsWebTransport()) {
    QUICHE_DLOG(INFO) << "session: SupportsWebTransport = "
                      << spdy_client_.client_session()->SupportsWebTransport()
                      << ", SupportsH3Datagram = "
                      << spdy_client_.client_session()->SupportsH3Datagram()
                      << ", OneRttKeysAvailable = "
                      << spdy_client_.client_session()->OneRttKeysAvailable();
    return absl::FailedPreconditionError(
        "Server does not support WebTransport");
  }
  auto* stream = static_cast<quic::QuicSpdyClientStream*>(
      spdy_client_.client_session()->CreateOutgoingBidirectionalStream());
  if (!stream) {
    return absl::InternalError("Could not open a CONNECT stream");
  }
  spdy_client_.set_store_response(true);

  spdy::Http2HeaderBlock headers;
  headers[":scheme"] = "https";
  headers[":authority"] = spdy_client_.server_id().host();
  headers[":path"] = path;
  headers[":method"] = "CONNECT";
  headers[":protocol"] = "webtransport";
  stream->SendRequest(std::move(headers), "", false);

  quic::WebTransportHttp3* web_transport = stream->web_transport();
  if (web_transport == nullptr) {
    return absl::InternalError("Failed to initialize WebTransport session");
  }

  MoqtSessionParameters parameters;
  parameters.version = MoqtVersion::kDraft03;
  parameters.perspective = quic::Perspective::IS_CLIENT,
  parameters.using_webtrans = true;
  parameters.path = "";
  parameters.deliver_partial_objects = false;

  // Ensure that we never have a dangling pointer to the session.
  MoqtSessionDeletedCallback deleted_callback =
      std::move(callbacks.session_deleted_callback);
  callbacks.session_deleted_callback =
      [this, old = std::move(deleted_callback)]() mutable {
        session_ = nullptr;
        std::move(old)();
      };

  auto session = std::make_unique<MoqtSession>(web_transport, parameters,
                                               std::move(callbacks));
  session_ = session.get();
  web_transport->SetVisitor(std::move(session));
  return absl::OkStatus();
}

}  // namespace moqt
