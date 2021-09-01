// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/masque/masque_epoll_client.h"
#include "absl/memory/memory.h"
#include "quic/masque/masque_client_session.h"
#include "quic/masque/masque_utils.h"

namespace quic {

MasqueEpollClient::MasqueEpollClient(
    QuicSocketAddress server_address,
    const QuicServerId& server_id,
    MasqueMode masque_mode,
    QuicEpollServer* epoll_server,
    std::unique_ptr<ProofVerifier> proof_verifier,
    const std::string& authority)
    : QuicClient(server_address,
                 server_id,
                 MasqueSupportedVersions(),
                 epoll_server,
                 std::move(proof_verifier)),
      masque_mode_(masque_mode),
      authority_(authority) {}

std::unique_ptr<QuicSession> MasqueEpollClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  QUIC_DLOG(INFO) << "Creating MASQUE session for "
                  << connection->connection_id();
  return std::make_unique<MasqueClientSession>(
      masque_mode_, *config(), supported_versions, connection, server_id(),
      crypto_config(), push_promise_index(), this);
}

MasqueClientSession* MasqueEpollClient::masque_client_session() {
  return static_cast<MasqueClientSession*>(QuicClient::session());
}

QuicConnectionId MasqueEpollClient::connection_id() {
  return masque_client_session()->connection_id();
}

// static
std::unique_ptr<MasqueEpollClient> MasqueEpollClient::Create(
    const std::string& host,
    int port,
    MasqueMode masque_mode,
    QuicEpollServer* epoll_server,
    std::unique_ptr<ProofVerifier> proof_verifier) {
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
  auto masque_client = absl::WrapUnique(new MasqueEpollClient(
      addr, server_id, masque_mode, epoll_server, std::move(proof_verifier),
      absl::StrCat(host, ":", port)));

  if (masque_client == nullptr) {
    QUIC_LOG(ERROR) << "Failed to create masque_client";
    return nullptr;
  }

  masque_client->set_initial_max_packet_length(kDefaultMaxPacketSize);
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

  if (masque_client->masque_mode() == MasqueMode::kLegacy) {
    // Construct the legacy mode init request.
    spdy::Http2HeaderBlock header_block;
    header_block[":method"] = "POST";
    header_block[":scheme"] = "https";
    header_block[":authority"] = masque_client->authority_;
    header_block[":path"] = "/.well-known/masque/init";
    std::string body = "foo";

    // Make sure to store the response, for later output.
    masque_client->set_store_response(true);

    // Send the MASQUE init command.
    masque_client->SendRequestAndWaitForResponse(header_block, body,
                                                 /*fin=*/true);

    if (!masque_client->connected()) {
      QUIC_LOG(ERROR)
          << "MASQUE init request caused connection failure. Error: "
          << QuicErrorCodeToString(masque_client->session()->error());
      return nullptr;
    }

    const int response_code = masque_client->latest_response_code();
    if (response_code != 200) {
      QUIC_LOG(ERROR) << "MASQUE init request failed with HTTP response code "
                      << response_code;
      return nullptr;
    }
  }
  return masque_client;
}

void MasqueEpollClient::OnSettingsReceived() {
  settings_received_ = true;
}

bool MasqueEpollClient::WaitUntilSettingsReceived() {
  while (connected() && !settings_received_) {
    network_helper()->RunEventLoop();
  }
  return connected() && settings_received_;
}

void MasqueEpollClient::UnregisterClientConnectionId(
    QuicConnectionId client_connection_id) {
  std::string body(client_connection_id.data(), client_connection_id.length());

  // Construct a GET or POST request for supplied URL.
  spdy::Http2HeaderBlock header_block;
  header_block[":method"] = "POST";
  header_block[":scheme"] = "https";
  header_block[":authority"] = authority_;
  header_block[":path"] = "/.well-known/masque/unregister";

  // Make sure to store the response, for later output.
  set_store_response(true);

  // Send the MASQUE unregister command.
  SendRequest(header_block, body, /*fin=*/true);
}

}  // namespace quic
