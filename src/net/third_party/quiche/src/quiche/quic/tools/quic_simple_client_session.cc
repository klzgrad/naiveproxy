// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_simple_client_session.h"

#include <memory>
#include <utility>

#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

QuicSimpleClientSession::QuicSimpleClientSession(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, QuicClientBase::NetworkHelper* network_helper,
    const QuicServerId& server_id, QuicCryptoClientConfig* crypto_config,
    bool drop_response_body, bool enable_web_transport)
    : QuicSimpleClientSession(config, supported_versions, connection,
                              /*visitor=*/nullptr, network_helper, server_id,
                              crypto_config, drop_response_body,
                              enable_web_transport) {}

QuicSimpleClientSession::QuicSimpleClientSession(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, QuicSession::Visitor* visitor,
    QuicClientBase::NetworkHelper* network_helper,
    const QuicServerId& server_id, QuicCryptoClientConfig* crypto_config,
    bool drop_response_body, bool enable_web_transport)
    : QuicSpdyClientSession(config, supported_versions, connection, visitor,
                            server_id, crypto_config),
      network_helper_(network_helper),
      drop_response_body_(drop_response_body),
      enable_web_transport_(enable_web_transport) {}

std::unique_ptr<QuicSpdyClientStream>
QuicSimpleClientSession::CreateClientStream() {
  auto stream = std::make_unique<QuicSimpleClientStream>(
      GetNextOutgoingBidirectionalStreamId(), this, BIDIRECTIONAL,
      drop_response_body_);
  stream->set_on_interim_headers(
      [this](const quiche::HttpHeaderBlock& headers) {
        on_interim_headers_(headers);
      });
  return stream;
}

WebTransportHttp3VersionSet
QuicSimpleClientSession::LocallySupportedWebTransportVersions() const {
  return enable_web_transport_ ? kDefaultSupportedWebTransportVersions
                               : WebTransportHttp3VersionSet();
}

HttpDatagramSupport QuicSimpleClientSession::LocalHttpDatagramSupport() {
  return enable_web_transport_ ? HttpDatagramSupport::kRfcAndDraft04
                               : HttpDatagramSupport::kNone;
}

void QuicSimpleClientSession::CreateContextForMultiPortPath(
    std::unique_ptr<MultiPortPathContextObserver> context_observer) {
  if (!network_helper_ || connection()->multi_port_stats() == nullptr) {
    return;
  }
  auto self_address = connection()->self_address();
  auto server_address = connection()->peer_address();
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

void QuicSimpleClientSession::MigrateToMultiPortPath(
    std::unique_ptr<QuicPathValidationContext> context) {
  auto* path_migration_context =
      static_cast<PathMigrationContext*>(context.get());
  MigratePath(path_migration_context->self_address(),
              path_migration_context->peer_address(),
              path_migration_context->ReleaseWriter(), /*owns_writer=*/true);
}

}  // namespace quic
