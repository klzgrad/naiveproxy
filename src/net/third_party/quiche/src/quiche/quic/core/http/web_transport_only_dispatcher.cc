// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/web_transport_only_dispatcher.h"

#include <memory>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/http/web_transport_only_server_session.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

std::unique_ptr<QuicSession> WebTransportOnlyDispatcher::CreateQuicSession(
    QuicConnectionId server_connection_id,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, absl::string_view alpn,
    const ParsedQuicVersion& version, const ParsedClientHello& parsed_chlo,
    ConnectionIdGeneratorInterface& connection_id_generator) {
  if (alpn.empty()) {
    return nullptr;
  }

  // TODO(vasilvv): handle ALPN.
  // TODO(vasilvv): support raw QUIC.

  auto connection = std::make_unique<QuicConnection>(
      server_connection_id, self_address, peer_address, helper(),
      alarm_factory(), writer(),
      /*owns_writer=*/false, Perspective::IS_SERVER,
      ParsedQuicVersionVector{version}, connection_id_generator);
  auto session = std::make_unique<WebTransportOnlyServerSession>(
      config(), GetSupportedVersions(), connection.release(), this,
      session_helper(), crypto_config(), compressed_certs_cache(),
      QuicPriorityType::kWebTransport);
  session->SetHandlerFactory(
      [this](webtransport::Session* session,
             const WebTransportIncomingRequestDetails& details) {
        return parameters_.handler_factory(session, details);
      });
  session->SetSubprotocolCallback(
      [this](absl::Span<const absl::string_view> subprotocols) {
        return parameters_.subprotocol_callback(subprotocols);
      });
  session->Initialize();
  return session;
}

}  // namespace quic
