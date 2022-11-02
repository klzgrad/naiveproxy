// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_client.h"

#include <utility>

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_epoll_alarm_factory.h"
#include "quiche/quic/core/quic_epoll_connection_helper.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_simple_client_session.h"

namespace quic {

QuicClient::QuicClient(QuicSocketAddress server_address,
                       const QuicServerId& server_id,
                       const ParsedQuicVersionVector& supported_versions,
                       QuicEpollServer* epoll_server,
                       std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicClient(
          server_address, server_id, supported_versions, QuicConfig(),
          epoll_server,
          std::make_unique<QuicClientEpollNetworkHelper>(epoll_server, this),
          std::move(proof_verifier), nullptr) {}

QuicClient::QuicClient(QuicSocketAddress server_address,
                       const QuicServerId& server_id,
                       const ParsedQuicVersionVector& supported_versions,
                       QuicEpollServer* epoll_server,
                       std::unique_ptr<ProofVerifier> proof_verifier,
                       std::unique_ptr<SessionCache> session_cache)
    : QuicClient(
          server_address, server_id, supported_versions, QuicConfig(),
          epoll_server,
          std::make_unique<QuicClientEpollNetworkHelper>(epoll_server, this),
          std::move(proof_verifier), std::move(session_cache)) {}

QuicClient::QuicClient(QuicSocketAddress server_address,
                       const QuicServerId& server_id,
                       const ParsedQuicVersionVector& supported_versions,
                       const QuicConfig& config, QuicEpollServer* epoll_server,
                       std::unique_ptr<ProofVerifier> proof_verifier,
                       std::unique_ptr<SessionCache> session_cache)
    : QuicClient(
          server_address, server_id, supported_versions, config, epoll_server,
          std::make_unique<QuicClientEpollNetworkHelper>(epoll_server, this),
          std::move(proof_verifier), std::move(session_cache)) {}

QuicClient::QuicClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions,
    QuicEpollServer* epoll_server,
    std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicClient(server_address, server_id, supported_versions, QuicConfig(),
                 epoll_server, std::move(network_helper),
                 std::move(proof_verifier), nullptr) {}

QuicClient::QuicClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicEpollServer* epoll_server,
    std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicClient(server_address, server_id, supported_versions, config,
                 epoll_server, std::move(network_helper),
                 std::move(proof_verifier), nullptr) {}

QuicClient::QuicClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicEpollServer* epoll_server,
    std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : QuicSpdyClientBase(
          server_id, supported_versions, config,
          new QuicEpollConnectionHelper(epoll_server, QuicAllocator::SIMPLE),
          new QuicEpollAlarmFactory(epoll_server), std::move(network_helper),
          std::move(proof_verifier), std::move(session_cache)) {
  set_server_address(server_address);
}

QuicClient::~QuicClient() = default;

std::unique_ptr<QuicSession> QuicClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  return std::make_unique<QuicSimpleClientSession>(
      *config(), supported_versions, connection, network_helper(), server_id(),
      crypto_config(), push_promise_index(), drop_response_body(),
      enable_web_transport());
}

QuicClientEpollNetworkHelper* QuicClient::epoll_network_helper() {
  return static_cast<QuicClientEpollNetworkHelper*>(network_helper());
}

const QuicClientEpollNetworkHelper* QuicClient::epoll_network_helper() const {
  return static_cast<const QuicClientEpollNetworkHelper*>(network_helper());
}

}  // namespace quic
