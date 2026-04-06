// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy client, which connects to a specified port and sends QUIC
// request to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_DEFAULT_CLIENT_H_
#define QUICHE_QUIC_TOOLS_QUIC_DEFAULT_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_path_context_factory.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_simple_client_session.h"
#include "quiche/quic/tools/quic_spdy_client_base.h"

namespace quic {

class QuicServerId;

namespace test {
class QuicDefaultClientPeer;
}  // namespace test

class QuicDefaultClient : public QuicSpdyClientBase {
 public:
  // An implementation that only creates path validation contexts but does not
  // support network handles or alternative networks.
  class QuicDefaultMigrationHelper : public QuicMigrationHelper {
   public:
    explicit QuicDefaultMigrationHelper(QuicDefaultClient& client)
        : client_(client) {}

    void OnMigrationToPathDone(
        std::unique_ptr<QuicClientPathValidationContext> context,
        bool success) override;

    std::unique_ptr<QuicPathContextFactory> CreateQuicPathContextFactory()
        override;

    QuicNetworkHandle FindAlternateNetwork(QuicNetworkHandle network) override;

    QuicNetworkHandle GetDefaultNetwork() override {
      return kInvalidNetworkHandle;
    }

    QuicNetworkHandle GetCurrentNetwork() override {
      return kInvalidNetworkHandle;
    }

    // Returns a specific address for a given network handle.
    virtual QuicIpAddress GetAddressForNetwork(QuicNetworkHandle network) const;

   private:
    QuicDefaultClient& client_;
  };

  // These will create their own QuicClientDefaultNetworkHelper.
  QuicDefaultClient(QuicSocketAddress server_address,
                    const QuicServerId& server_id,
                    const ParsedQuicVersionVector& supported_versions,
                    QuicEventLoop* event_loop,
                    std::unique_ptr<ProofVerifier> proof_verifier);
  QuicDefaultClient(QuicSocketAddress server_address,
                    const QuicServerId& server_id,
                    const ParsedQuicVersionVector& supported_versions,
                    QuicEventLoop* event_loop,
                    std::unique_ptr<ProofVerifier> proof_verifier,
                    std::unique_ptr<SessionCache> session_cache);
  QuicDefaultClient(QuicSocketAddress server_address,
                    const QuicServerId& server_id,
                    const ParsedQuicVersionVector& supported_versions,
                    const QuicConfig& config, QuicEventLoop* event_loop,
                    std::unique_ptr<ProofVerifier> proof_verifier,
                    std::unique_ptr<SessionCache> session_cache);
  // This will take ownership of a passed in network primitive.
  QuicDefaultClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
      std::unique_ptr<ProofVerifier> proof_verifier);
  QuicDefaultClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      const QuicConfig& config, QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
      std::unique_ptr<ProofVerifier> proof_verifier);
  QuicDefaultClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      const QuicConfig& config, QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
      std::unique_ptr<ProofVerifier> proof_verifier,
      std::unique_ptr<SessionCache> session_cache);
  QuicDefaultClient(const QuicDefaultClient&) = delete;
  QuicDefaultClient& operator=(const QuicDefaultClient&) = delete;

  ~QuicDefaultClient() override;

  // QuicSpdyClientBase overrides.
  bool Initialize() override;
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  // Exposed for QUIC tests.
  int GetLatestFD() const { return default_network_helper()->GetLatestFD(); }

  QuicClientDefaultNetworkHelper* default_network_helper();
  const QuicClientDefaultNetworkHelper* default_network_helper() const;

  // Overridden to skip handling server preferred address and path degrading
  // if the migration manager has already handled them according to the
  // migration config.
  void OnServerPreferredAddressAvailable(
      const QuicSocketAddress& server_preferred_address) override;
  void OnPathDegrading() override;

  // Must be called before `Connect()`.
  void set_migration_config(
      const QuicConnectionMigrationConfig& migration_config) {
    migration_config_ = migration_config;
  }

 protected:
  // Called during Initialize() to create the migration helper.
  virtual std::unique_ptr<QuicMigrationHelper> CreateQuicMigrationHelper();

 private:
  std::unique_ptr<QuicMigrationHelper> migration_helper_;
  QuicConnectionMigrationConfig migration_config_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_DEFAULT_CLIENT_H_
