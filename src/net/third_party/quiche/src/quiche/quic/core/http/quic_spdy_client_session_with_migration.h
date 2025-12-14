// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_SESSION_WITH_MIGRATION_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_SESSION_WITH_MIGRATION_H_

#include <memory>

#include "quiche/quic/core/http/quic_connection_migration_manager.h"
#include "quiche/quic/core/http/quic_spdy_client_session_base.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_force_blockable_packet_writer.h"
#include "quiche/quic/core/quic_path_context_factory.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// A client session implementation that supports connection migration upon these
// events under IETF versions:
// 1. platform's network change signals
// 2. packetwrite error
// 3. QuicConnection detected path degrading
// 4. received server preferred address
// 5. handshake completion on a non-default network.
// TODO(danzh): Rename this class to be something more like a base class.
class QUICHE_EXPORT QuicSpdyClientSessionWithMigration
    : public QuicSpdyClientSessionBase {
 public:
  // `writer` must be the same as the connection's writer if any type of
  // migration is enabled. Otherwise, it can also be nullptr.
  QuicSpdyClientSessionWithMigration(
      QuicConnection* connection, QuicForceBlockablePacketWriter* writer,
      QuicSession::Visitor* visitor, const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      QuicNetworkHandle default_network, QuicNetworkHandle current_network,
      std::unique_ptr<QuicPathContextFactory> path_context_factory,
      const QuicConnectionMigrationConfig& migration_config,
      QuicPriorityType priority_type);

  ~QuicSpdyClientSessionWithMigration() override;

  // Called before connection gets closed upon a migration failure.
  virtual void OnConnectionToBeClosedDueToMigrationError(
      MigrationCause migration_cause, QuicErrorCode quic_error) = 0;

  // Returns a network handle which is different from the given |network|;
  virtual QuicNetworkHandle FindAlternateNetwork(QuicNetworkHandle network) = 0;

  // Close non-migratable streams in both directions by sending reset stream to
  // peer when connection migration attempts to migrate to the alternate
  // network.
  virtual void ResetNonMigratableStreams() = 0;

  // Called when there is no new network available to migrate to upon write
  // error or network disconnect.
  virtual void OnNoNewNetworkForMigration() = 0;

  // Mark the session draining to not accept any new requests.
  virtual void StartDraining() = 0;

  // Called before using the given |context| to probe a path.
  virtual void PrepareForProbingOnPath(QuicPathValidationContext& context) = 0;

  virtual bool IsSessionProxied() const = 0;

  // Returns the time elapsed since the latest stream closure.
  quic::QuicTimeDelta TimeSinceLastStreamClose();

  // QuicSpdyClientSessionBase
  void OnPathDegrading() override;
  void OnTlsHandshakeComplete() override;
  void SetDefaultEncryptionLevel(EncryptionLevel level) override;
  void OnServerPreferredAddressAvailable(
      const quic::QuicSocketAddress& server_preferred_address) override;
  bool MaybeMitigateWriteError(const WriteResult& write_result) override;
  void OnStreamClosed(QuicStreamId stream_id) override;

  // Migrates session onto the new path, i.e. changing the default writer and
  // network.
  // Returns true on successful migration.
  bool MigrateToNewPath(
      std::unique_ptr<QuicClientPathValidationContext> path_context);

  void SetMigrationDebugVisitor(
      quic::QuicConnectionMigrationDebugVisitor* visitor);

  const QuicConnectionMigrationConfig& GetConnectionMigrationConfig() const;

  QuicConnectionMigrationManager& migration_manager() {
    return migration_manager_;
  }

  const QuicConnectionMigrationManager& migration_manager() const {
    return migration_manager_;
  }

  QuicForceBlockablePacketWriter* writer() { return writer_; }

 private:
  // Called in MigrateToNewPath() prior to calling MigratePath().
  // Return false if MigratePath() should be skipped.
  virtual bool PrepareForMigrationToPath(
      QuicClientPathValidationContext& context) = 0;

  // Called in MigrateToNewPath() after MigratePath() for clean up.
  virtual void OnMigrationToPathDone(
      std::unique_ptr<QuicClientPathValidationContext> context,
      bool success) = 0;

  std::unique_ptr<QuicPathContextFactory> path_context_factory_;
  QuicConnectionMigrationManager migration_manager_;
  QuicForceBlockablePacketWriter* writer_;
  QuicTime most_recent_stream_close_time_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_CLIENT_SESSION_WITH_MIGRATION_H_
