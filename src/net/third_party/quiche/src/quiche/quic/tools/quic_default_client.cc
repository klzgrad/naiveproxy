// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_default_client.h"

#include <memory>
#include <utility>

#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_simple_client_session.h"

namespace quic {

QuicDefaultClient::QuicDefaultClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicDefaultClient(
          server_address, server_id, supported_versions, QuicConfig(),
          event_loop,
          std::make_unique<QuicClientDefaultNetworkHelper>(event_loop, this),
          std::move(proof_verifier), nullptr) {}

QuicDefaultClient::QuicDefaultClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : QuicDefaultClient(
          server_address, server_id, supported_versions, QuicConfig(),
          event_loop,
          std::make_unique<QuicClientDefaultNetworkHelper>(event_loop, this),
          std::move(proof_verifier), std::move(session_cache)) {}

QuicDefaultClient::QuicDefaultClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : QuicDefaultClient(
          server_address, server_id, supported_versions, config, event_loop,
          std::make_unique<QuicClientDefaultNetworkHelper>(event_loop, this),
          std::move(proof_verifier), std::move(session_cache)) {}

QuicDefaultClient::QuicDefaultClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions,
    QuicEventLoop* event_loop,
    std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicDefaultClient(server_address, server_id, supported_versions,
                        QuicConfig(), event_loop, std::move(network_helper),
                        std::move(proof_verifier), nullptr) {}

QuicDefaultClient::QuicDefaultClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicEventLoop* event_loop,
    std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicDefaultClient(server_address, server_id, supported_versions, config,
                        event_loop, std::move(network_helper),
                        std::move(proof_verifier), nullptr) {}

QuicDefaultClient::QuicDefaultClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicEventLoop* event_loop,
    std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : QuicSpdyClientBase(server_id, supported_versions, config,
                         new QuicDefaultConnectionHelper(),
                         event_loop->CreateAlarmFactory().release(),
                         std::move(network_helper), std::move(proof_verifier),
                         std::move(session_cache)) {
  set_server_address(server_address);
}

QuicDefaultClient::~QuicDefaultClient() = default;

std::unique_ptr<QuicSession> QuicDefaultClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  return std::make_unique<QuicSimpleClientSession>(
      *config(), supported_versions, connection, this, network_helper(),
      server_id(), crypto_config(), drop_response_body(),
      enable_web_transport());
}

QuicClientDefaultNetworkHelper* QuicDefaultClient::default_network_helper() {
  return static_cast<QuicClientDefaultNetworkHelper*>(network_helper());
}

const QuicClientDefaultNetworkHelper*
QuicDefaultClient::default_network_helper() const {
  return static_cast<const QuicClientDefaultNetworkHelper*>(network_helper());
}

}  // namespace quic
