// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_epoll_client.h"

#include <string>

#include "absl/memory/memory.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/tools/quic_url.h"

namespace quic {

MasqueEpollClient::MasqueEpollClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    MasqueMode masque_mode, QuicEpollServer* epoll_server,
    std::unique_ptr<ProofVerifier> proof_verifier,
    const std::string& uri_template)
    : QuicClient(server_address, server_id, MasqueSupportedVersions(),
                 epoll_server, std::move(proof_verifier)),
      masque_mode_(masque_mode),
      uri_template_(uri_template) {}

std::unique_ptr<QuicSession> MasqueEpollClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  QUIC_DLOG(INFO) << "Creating MASQUE session for "
                  << connection->connection_id();
  return std::make_unique<MasqueClientSession>(
      masque_mode_, uri_template_, *config(), supported_versions, connection,
      server_id(), crypto_config(), push_promise_index(), this);
}

MasqueClientSession* MasqueEpollClient::masque_client_session() {
  return static_cast<MasqueClientSession*>(QuicClient::session());
}

QuicConnectionId MasqueEpollClient::connection_id() {
  return masque_client_session()->connection_id();
}

std::string MasqueEpollClient::authority() const {
  QuicUrl url(uri_template_);
  return absl::StrCat(url.host(), ":", url.port());
}

// static
std::unique_ptr<MasqueEpollClient> MasqueEpollClient::Create(
    const std::string& uri_template, MasqueMode masque_mode,
    QuicEpollServer* epoll_server,
    std::unique_ptr<ProofVerifier> proof_verifier) {
  QuicUrl url(uri_template);
  std::string host = url.host();
  uint16_t port = url.port();
  // Build the masque_client, and try to connect.
  QuicSocketAddress addr = tools::LookupAddress(host, absl::StrCat(port));
  if (!addr.IsInitialized()) {
    QUIC_LOG(ERROR) << "Unable to resolve address: " << host;
    return nullptr;
  }
  QuicServerId server_id(host, port);
  // Use absl::WrapUnique(new MasqueEpollClient(...)) instead of
  // std::make_unique<MasqueEpollClient>(...) because the constructor for
  // MasqueEpollClient is private and therefore not accessible from make_unique.
  auto masque_client = absl::WrapUnique(
      new MasqueEpollClient(addr, server_id, masque_mode, epoll_server,
                            std::move(proof_verifier), uri_template));

  if (masque_client == nullptr) {
    QUIC_LOG(ERROR) << "Failed to create masque_client";
    return nullptr;
  }

  masque_client->set_initial_max_packet_length(kMasqueMaxOuterPacketSize);
  masque_client->set_drop_response_body(false);
  if (!masque_client->Initialize()) {
    QUIC_LOG(ERROR) << "Failed to initialize masque_client";
    return nullptr;
  }
  if (!masque_client->Connect()) {
    QuicErrorCode error = masque_client->session()->error();
    QUIC_LOG(ERROR) << "Failed to connect to " << host << ":" << port
                    << ". Error: " << QuicErrorCodeToString(error);
    return nullptr;
  }

  if (!masque_client->WaitUntilSettingsReceived()) {
    QUIC_LOG(ERROR) << "Failed to receive settings";
    return nullptr;
  }

  return masque_client;
}

void MasqueEpollClient::OnSettingsReceived() { settings_received_ = true; }

bool MasqueEpollClient::WaitUntilSettingsReceived() {
  while (connected() && !settings_received_) {
    network_helper()->RunEventLoop();
  }
  return connected() && settings_received_;
}

}  // namespace quic
