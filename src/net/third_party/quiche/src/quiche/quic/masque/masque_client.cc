// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_client.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

MasqueClient::MasqueClient(QuicSocketAddress server_address,
                           const QuicServerId& server_id,
                           MasqueMode masque_mode, QuicEventLoop* event_loop,
                           std::unique_ptr<ProofVerifier> proof_verifier,
                           const std::string& uri_template)
    : QuicDefaultClient(server_address, server_id, MasqueSupportedVersions(),
                        event_loop, std::move(proof_verifier)),
      masque_mode_(masque_mode),
      uri_template_(uri_template) {
  QUICHE_CHECK(!QuicUrl(uri_template_).host().empty());
}

MasqueClient::MasqueClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    MasqueMode masque_mode, QuicEventLoop* event_loop, const QuicConfig& config,
    std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    const std::string& uri_template)
    : QuicDefaultClient(server_address, server_id, MasqueSupportedVersions(),
                        config, event_loop, std::move(network_helper),
                        std::move(proof_verifier)),
      masque_mode_(masque_mode),
      uri_template_(uri_template) {
  QUICHE_CHECK(!QuicUrl(uri_template_).host().empty());
}

MasqueClient::MasqueClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    QuicEventLoop* event_loop, const QuicConfig& config,
    std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicDefaultClient(server_address, server_id, MasqueSupportedVersions(),
                        config, event_loop, std::move(network_helper),
                        std::move(proof_verifier)) {}

std::unique_ptr<QuicSession> MasqueClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  QUIC_DLOG(INFO) << "Creating MASQUE session for "
                  << connection->connection_id();
  return std::make_unique<MasqueClientSession>(
      masque_mode_, uri_template_, *config(), supported_versions, connection,
      server_id(), crypto_config(), this);
}

MasqueClientSession* MasqueClient::masque_client_session() {
  return static_cast<MasqueClientSession*>(QuicDefaultClient::session());
}

QuicConnectionId MasqueClient::connection_id() {
  return masque_client_session()->connection_id();
}

std::string MasqueClient::authority() const {
  QuicUrl url(uri_template_);
  return absl::StrCat(url.host(), ":", url.port());
}

// static
std::unique_ptr<MasqueClient> MasqueClient::Create(
    const std::string& uri_template, MasqueMode masque_mode,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier) {
  QuicUrl url(uri_template);
  std::string host = url.host();
  if (host.empty()) {
    QUIC_LOG(ERROR) << "Failed to parse URI template \"" << uri_template
                    << "\"";
    return nullptr;
  }
  uint16_t port = url.port();
  // Build the masque_client, and try to connect.
  QuicSocketAddress addr = tools::LookupAddress(host, absl::StrCat(port));
  if (!addr.IsInitialized()) {
    QUIC_LOG(ERROR) << "Unable to resolve address: " << host;
    return nullptr;
  }
  QuicServerId server_id(host, port);
  // Use absl::WrapUnique(new MasqueClient(...)) instead of
  // std::make_unique<MasqueClient>(...) because the constructor for
  // MasqueClient is private and therefore not accessible from make_unique.
  auto masque_client = absl::WrapUnique(
      new MasqueClient(addr, server_id, masque_mode, event_loop,
                       std::move(proof_verifier), uri_template));

  if (masque_client == nullptr) {
    QUIC_LOG(ERROR) << "Failed to create masque_client";
    return nullptr;
  }
  if (!masque_client->Prepare(kDefaultMaxPacketSizeForTunnels)) {
    QUIC_LOG(ERROR) << "Failed to prepare MASQUE client to " << host << ":"
                    << port;
    return nullptr;
  }
  return masque_client;
}

bool MasqueClient::Prepare(QuicByteCount max_packet_size) {
  set_initial_max_packet_length(max_packet_size);
  set_drop_response_body(false);
  if (!Initialize()) {
    QUIC_LOG(ERROR) << "Failed to initialize MASQUE client";
    return false;
  }
  if (!Connect()) {
    QuicErrorCode error = session()->error();
    QUIC_LOG(ERROR) << "Failed to connect. Error: "
                    << QuicErrorCodeToString(error);
    return false;
  }
  if (!WaitUntilSettingsReceived()) {
    QUIC_LOG(ERROR) << "Failed to receive settings";
    return false;
  }
  return true;
}

void MasqueClient::OnSettingsReceived() { settings_received_ = true; }

bool MasqueClient::WaitUntilSettingsReceived() {
  while (connected() && !settings_received_) {
    network_helper()->RunEventLoop();
  }
  return connected() && settings_received_;
}

}  // namespace quic
