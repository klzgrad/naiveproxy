// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_default_client.h"

#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/http/quic_connection_migration_manager.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_path_context_factory.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_base.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_simple_client_session.h"
#include "quiche/quic/tools/quic_spdy_client_base.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

// A path context which owns the writer.
class QUIC_EXPORT_PRIVATE PathValidationContextForMigrationManager
    : public QuicClientPathValidationContext {
 public:
  PathValidationContextForMigrationManager(
      std::unique_ptr<QuicForceBlockablePacketWriter> writer,
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, QuicNetworkHandle network)
      : QuicClientPathValidationContext(self_address, peer_address, network),
        alternative_writer_(std::move(writer)) {}

  QuicForceBlockablePacketWriter* ForceBlockableWriterToUse() override {
    return alternative_writer_.get();
  }
  bool ShouldConnectionOwnWriter() const override { return false; }

  QuicPacketWriter* ReleaseWriter() { return alternative_writer_.release(); }

 private:
  std::unique_ptr<QuicForceBlockablePacketWriter> alternative_writer_;
};

class QuicDefaultPathContextFactory : public QuicPathContextFactory {
 public:
  QuicDefaultPathContextFactory(
      QuicDefaultClient::QuicDefaultMigrationHelper& migration_helper,
      QuicClientBase::NetworkHelper* network_helper)
      : migration_helper_(migration_helper), network_helper_(network_helper) {}

  void CreatePathValidationContext(
      QuicNetworkHandle network, QuicSocketAddress peer_address,
      std::unique_ptr<CreationResultDelegate> result_delegate) override {
    QuicIpAddress self_address =
        migration_helper_.GetAddressForNetwork(network);
    if (network_helper_ == nullptr || !network_helper_->CreateUDPSocketAndBind(
                                          peer_address, self_address, 0)) {
      result_delegate->OnCreationFailed(network,
                                        "Failed to create UDP socket.");
      QUICHE_LOG(ERROR) << "Failed to create UDP socket.";
      return;
    }
    QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
    if (writer == nullptr) {
      result_delegate->OnCreationFailed(network,
                                        "Failed to create QuicPacketWriter.");
      QUICHE_LOG(ERROR) << "Failed to create QuicPacketWriter.";
      return;
    }
    auto force_blockable_writer =
        std::make_unique<QuicForceBlockablePacketWriter>();
    force_blockable_writer->set_writer(writer);
    return result_delegate->OnCreationSucceeded(
        std::make_unique<PathValidationContextForMigrationManager>(
            std::move(force_blockable_writer),
            network_helper_->GetLatestClientAddress(), peer_address, network));
  }

 private:
  QuicDefaultClient::QuicDefaultMigrationHelper& migration_helper_;
  QuicClientBase::NetworkHelper* network_helper_;
};

std::unique_ptr<QuicPathContextFactory>
QuicDefaultClient::QuicDefaultMigrationHelper::CreateQuicPathContextFactory() {
  return std::make_unique<QuicDefaultPathContextFactory>(
      *this, client_.network_helper());
}

void QuicDefaultClient::QuicDefaultMigrationHelper::OnMigrationToPathDone(
    std::unique_ptr<QuicClientPathValidationContext> context, bool success) {
  if (success) {
    auto migration_context =
        absl::WrapUnique(static_cast<PathValidationContextForMigrationManager*>(
            context.release()));
    client_.set_writer(migration_context->ReleaseWriter());
  } else {
    QUICHE_LOG(ERROR) << "Failed to migrate to path.";
  }
}

QuicIpAddress
QuicDefaultClient::QuicDefaultMigrationHelper::GetAddressForNetwork(
    QuicNetworkHandle network) const {
  QUICHE_BUG_IF(network_handle_not_supported, network != kInvalidNetworkHandle)
      << "network handle is not supported on this";
  return client_.session()->connection()->self_address().host();
}

QuicNetworkHandle
QuicDefaultClient::QuicDefaultMigrationHelper::FindAlternateNetwork(
    QuicNetworkHandle /*network*/) {
  QUICHE_BUG(alternative_network_not_supported)
      << "Alternative network interface is not supported on this client.";
  return kInvalidNetworkHandle;
}

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

void QuicDefaultClient::OnServerPreferredAddressAvailable(
    const QuicSocketAddress& server_preferred_address) {
  if (!handle_migration_in_session()) {
    QuicSpdyClientBase::OnServerPreferredAddressAvailable(
        server_preferred_address);
  }
}

void QuicDefaultClient::OnPathDegrading() {
  if (!handle_migration_in_session()) {
    QuicSpdyClientBase::OnPathDegrading();
  }
}

QuicDefaultClient::~QuicDefaultClient() = default;

bool QuicDefaultClient::Initialize() {
  migration_helper_ = CreateQuicMigrationHelper();
  return QuicSpdyClientBase::Initialize();
}

std::unique_ptr<QuicMigrationHelper>
QuicDefaultClient::CreateQuicMigrationHelper() {
  return std::make_unique<QuicDefaultMigrationHelper>(*this);
}

std::unique_ptr<QuicSession> QuicDefaultClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  if (handle_migration_in_session()) {
    return std::make_unique<QuicSimpleClientSession>(
        *config(), supported_versions, connection, this,
        static_cast<QuicForceBlockablePacketWriter*>(connection->writer()),
        migration_helper_.get(), migration_config_, network_helper(),
        server_id(), crypto_config(), drop_response_body(),
        enable_web_transport());
  }
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
