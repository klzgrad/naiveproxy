// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/web_transport_only_client.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/http/quic_connection_migration_manager.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_base.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_event_loop_tools.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/web_transport/web_transport_headers.h"

namespace quic {

WebTransportOnlyClient::Session::Session(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, QuicClientBase::NetworkHelper* network_helper,
    const QuicServerId& server_id, QuicCryptoClientConfig* crypto_config)
    : QuicSpdyClientSession(
          config, supported_versions, connection,
          /*visitor=*/nullptr, /*writer=*/nullptr,
          /*migration_helper=*/nullptr,
          QuicConnectionMigrationConfig{.allow_server_preferred_address =
                                            false},
          server_id, crypto_config, QuicPriorityType::kWebTransport),
      network_helper_(network_helper) {}

WebTransportHttp3VersionSet
WebTransportOnlyClient::Session::LocallySupportedWebTransportVersions() const {
  return kDefaultSupportedWebTransportVersions;
}

HttpDatagramSupport
WebTransportOnlyClient::Session::LocalHttpDatagramSupport() {
  return HttpDatagramSupport::kRfcAndDraft04;
}

// TODO(vasilvv): the connection migration code below comes from
// QuicSimpleClientSession; should it live in QuicClientBase?
void WebTransportOnlyClient::Session::CreateContextForMultiPortPath(
    std::unique_ptr<MultiPortPathContextObserver> context_observer) {
  if (!network_helper_ || connection()->multi_port_stats() == nullptr) {
    return;
  }
  QuicSocketAddress self_address = connection()->self_address();
  QuicSocketAddress server_address = connection()->peer_address();
  if (!network_helper_->CreateUDPSocketAndBind(
          server_address, self_address.host(), self_address.port() + 1)) {
    return;
  }
  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  if (writer == nullptr) {
    return;
  }
  context_observer->OnMultiPortPathContextAvailable(
      std::make_unique<PathMigrationContext>(
          std::unique_ptr<QuicPacketWriter>(writer),
          network_helper_->GetLatestClientAddress(), peer_address()));
}

void WebTransportOnlyClient::Session::MigrateToMultiPortPath(
    std::unique_ptr<QuicPathValidationContext> context) {
  auto* path_migration_context =
      static_cast<PathMigrationContext*>(context.get());
  MigratePath(path_migration_context->self_address(),
              path_migration_context->peer_address(),
              path_migration_context->ReleaseWriter(), /*owns_writer=*/true);
}

WebTransportOnlyClient::WebTransportOnlyClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicEventLoop* event_loop,
    std::unique_ptr<QuicClientDefaultNetworkHelper> absl_nullable
    network_helper,
    std::unique_ptr<ProofVerifier> absl_nonnull proof_verifier,
    std::unique_ptr<SessionCache> absl_nullable session_cache)
    : QuicClientBase(server_id, supported_versions, config,
                     new QuicDefaultConnectionHelper(),
                     event_loop->CreateAlarmFactory().release(),
                     network_helper != nullptr
                         ? std::move(network_helper)
                         : std::make_unique<QuicClientDefaultNetworkHelper>(
                               event_loop, this),
                     std::move(proof_verifier), std::move(session_cache)) {
  set_server_address(server_address);
}

WebTransportOnlyClient::~WebTransportOnlyClient() { ResetSession(); }

absl::Status WebTransportOnlyClient::ConnectSync(
    std::string path, VisitorFactory visitor_factory,
    absl::Span<const std::string> subprotocols,
    const quiche::HttpHeaderBlock& extra_headers) {
  if (!Initialize()) {
    return absl::InternalError("Failed to initialize the client");
  }
  if (!QuicClientBase::Connect()) {
    return absl::InternalError("Failed to establish a QUIC connection");
  }
  bool settings_received =
      ProcessEventsUntil(default_network_helper()->event_loop(),
                         [&] { return client_session()->settings_received(); });
  if (!settings_received) {
    return absl::DeadlineExceededError(
        "Timed out while waiting for HTTP/3 SETTINGS");
  }
  if (!client_session()->SupportsWebTransport()) {
    return absl::FailedPreconditionError(
        "QUIC server does not support WebTransport");
  }
  auto* stream = absl::down_cast<QuicSpdyClientStream*>(
      client_session()->CreateOutgoingBidirectionalStream());
  if (!stream) {
    return absl::InternalError("Failed to create a CONNECT request stream");
  }

  quiche::HttpHeaderBlock headers = extra_headers.Clone();
  headers[":scheme"] = "https";
  headers[":authority"] = server_id().host();
  headers[":path"] = std::move(path);
  headers[":method"] = "CONNECT";
  headers[":protocol"] = "webtransport";
  if (!subprotocols.empty()) {
    absl::StatusOr<std::string> serialized =
        webtransport::SerializeSubprotocolRequestHeader(subprotocols);
    if (!serialized.ok()) {
      return absl::InvalidArgumentError("Invalid subprotocol name supplied");
    }
    headers["wt-available-protocols"] = *serialized;
  }
  stream->SendRequest(std::move(headers), "", false);

  WebTransportHttp3* web_transport = stream->web_transport();
  if (!web_transport) {
    return absl::InternalError(
        "Failed to associate a WebTransport session with an HTTP request");
  }
  web_transport->SetVisitor(std::move(visitor_factory)(web_transport));
  return absl::OkStatus();
}

QuicClientDefaultNetworkHelper*
WebTransportOnlyClient::default_network_helper() {
  return absl::down_cast<QuicClientDefaultNetworkHelper*>(network_helper());
}

WebTransportOnlyClient::Session* WebTransportOnlyClient::client_session() {
  return absl::down_cast<Session*>(QuicClientBase::session());
}

void WebTransportOnlyClient::InitializeSession() {
  client_session()->Initialize();
  client_session()->CryptoConnect();
}

bool WebTransportOnlyClient::EarlyDataAccepted() {
  return client_session()->EarlyDataAccepted();
}

bool WebTransportOnlyClient::ReceivedInchoateReject() {
  return client_session()->ReceivedInchoateReject();
}

int WebTransportOnlyClient::GetNumSentClientHellosFromSession() {
  return client_session()->GetNumSentClientHellos();
}

int WebTransportOnlyClient::GetNumReceivedServerConfigUpdatesFromSession() {
  return client_session()->GetNumReceivedServerConfigUpdates();
}

std::unique_ptr<QuicSession> WebTransportOnlyClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  return std::make_unique<Session>(*config(), supported_versions, connection,
                                   network_helper(), server_id(),
                                   crypto_config());
}

bool WebTransportOnlyClient::HasActiveRequests() {
  return client_session()->HasActiveRequestStreams();
}

}  // namespace quic
