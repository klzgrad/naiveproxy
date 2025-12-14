// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_CONNECTION_MIGRATION_MANAGER_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_CONNECTION_MIGRATION_MANAGER_H_

#include <cstddef>
#include <list>
#include <memory>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_path_context_factory.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

namespace test {
class QuicConnectionMigrationManagerPeer;
}  // namespace test

class QuicSpdyClientSessionWithMigration;

// Result of a session migration attempt.
enum class MigrationResult {
  SUCCESS,         // Migration succeeded.
  NO_NEW_NETWORK,  // Migration failed since no new network was found.
  FAILURE,         // Migration failed for other reasons.
};

// Cause of a migration.
enum class MigrationCause {
  UNKNOWN_CAUSE,                              // Not migrating.
  ON_NETWORK_CONNECTED,                       // No probing.
  ON_NETWORK_DISCONNECTED,                    // No probing.
  ON_WRITE_ERROR,                             // No probing.
  ON_NETWORK_MADE_DEFAULT,                    // With probing.
  ON_MIGRATE_BACK_TO_DEFAULT_NETWORK,         // With probing.
  CHANGE_NETWORK_ON_PATH_DEGRADING,           // With probing.
  CHANGE_PORT_ON_PATH_DEGRADING,              // With probing.
  NEW_NETWORK_CONNECTED_POST_PATH_DEGRADING,  // With probing.
  ON_SERVER_PREFERRED_ADDRESS_AVAILABLE,      // With probing.
};

// Result of connection migration.
enum class QuicConnectionMigrationStatus {
  MIGRATION_STATUS_SUCCESS,
  MIGRATION_STATUS_NO_MIGRATABLE_STREAMS,
  MIGRATION_STATUS_ALREADY_MIGRATED,
  MIGRATION_STATUS_INTERNAL_ERROR,
  MIGRATION_STATUS_TOO_MANY_CHANGES,
  MIGRATION_STATUS_NON_MIGRATABLE_STREAM,
  MIGRATION_STATUS_NOT_ENABLED,
  MIGRATION_STATUS_NO_ALTERNATE_NETWORK,
  MIGRATION_STATUS_ON_PATH_DEGRADING_DISABLED,
  MIGRATION_STATUS_DISABLED_BY_CONFIG,
  MIGRATION_STATUS_PATH_DEGRADING_NOT_ENABLED,
  MIGRATION_STATUS_TIMEOUT,
  MIGRATION_STATUS_ON_WRITE_ERROR_DISABLED,
  MIGRATION_STATUS_PATH_DEGRADING_BEFORE_HANDSHAKE_CONFIRMED,
  MIGRATION_STATUS_IDLE_MIGRATION_TIMEOUT,
  MIGRATION_STATUS_NO_UNUSED_CONNECTION_ID,
  MIGRATION_STATUS_MAX
};

// Result of a connectivity probing attempt.
enum class ProbingResult {
  PENDING,                          // Probing started, pending result.
  DISABLED_WITH_IDLE_SESSION,       // Probing disabled with idle session.
  DISABLED_BY_CONFIG,               // Probing disabled by config.
  DISABLED_BY_NON_MIGRABLE_STREAM,  // Probing disabled by special stream.
  INTERNAL_ERROR,                   // Probing failed for internal reason.
};

struct QUICHE_NO_EXPORT QuicConnectionMigrationConfig {
  // Whether to probe and migrate to a different network upon path degrading in
  // addition to the underlying platform's network change signals and write
  // error which usually come later. If migrate_session_on_network_change is
  // false, this must be false.
  bool migrate_session_early = false;
  // Whether to probe and migrate to a different port when migrating to a
  // different network is not allowed upon path degrading.
  bool allow_port_migration = false;
  // Whether to migrate a session with no in-flight requests to a different
  // network or port.
  bool migrate_idle_session = false;
  // Session can be migrated if its idle time is within this period.
  QuicTimeDelta idle_migration_period = QuicTimeDelta::FromSeconds(30);
  // Maximum time a connection is allowed to stay on a non-default network
  // before migrating back to the default network.
  QuicTimeDelta max_time_on_non_default_network =
      QuicTimeDelta::FromSeconds(128);
  // Maximum allowed number of migrations to non-default network triggered by
  // packet write error per default network.
  int max_migrations_to_non_default_network_on_write_error = 5;
  // Maximum allowed number of migrations to non-default network triggered by
  // path degrading per default network.
  int max_migrations_to_non_default_network_on_path_degrading = 5;
  // Maximum number of port migrations allowed per QUIC session.
  int max_port_migrations_per_session = 4;
  // Whether to migrate to a different network upon the underlying platform's
  // network change signals and write error.
  bool migrate_session_on_network_change = false;

  // Below are optional experimental features.
  bool ignore_disconnect_signal_during_probing = true;
  bool disable_blackhole_detection_on_immediate_migrate = true;
  bool allow_server_preferred_address = true;
};

class QUICHE_EXPORT QuicConnectionMigrationDebugVisitor {
 public:
  virtual ~QuicConnectionMigrationDebugVisitor() = default;

  virtual void OnNetworkConnected(QuicNetworkHandle network) = 0;
  virtual void OnConnectionMigrationAfterNetworkConnected(
      QuicNetworkHandle network) = 0;
  virtual void OnWaitingForNewNetworkToMigrate() = 0;
  virtual void OnWaitingForNewNetworkSucceeded(QuicNetworkHandle network) = 0;
  virtual void OnWaitForNetworkFailed() = 0;
  virtual void OnNetworkDisconnected(
      QuicNetworkHandle disconnected_network) = 0;
  virtual void OnConnectionMigrationAfterNetworkDisconnected(
      QuicNetworkHandle disconnected_network) = 0;
  virtual void OnConnectionMigrationAfterWriteError(
      QuicNetworkHandle current_network) = 0;
  virtual void OnConnectionMigrationStartingAfterEvent(
      absl::string_view event_name) = 0;
  virtual void OnConnectionMigrationStarted() = 0;
  virtual void OnPortMigrationStarting() = 0;
  virtual void OnPortMigrationStarted() = 0;
  virtual void OnConnectionMigrationBackToDefaultNetwork(
      int num_migration_back_retries) = 0;
  virtual void OnProbeResult(QuicNetworkHandle probed_network,
                             QuicSocketAddress peer_address, bool success) = 0;
  virtual void OnConnectionMigrationFailedAfterProbe() = 0;
  virtual void OnConnectionMigrationSucceededAfterProbe(
      QuicNetworkHandle probed_network) = 0;
  virtual void OnConnectionMigrationFailed(MigrationCause migration_cause,
                                           QuicConnectionId connection_id,
                                           absl::string_view details) = 0;
  virtual void OnProbingServerPreferredAddressStarting() = 0;
  virtual void OnProbingServerPreferredAddressStarted() = 0;
  virtual void OnNetworkMadeDefault(QuicNetworkHandle network) = 0;
  virtual void OnConnectionMigrationAfterNewDefaultNetwork(
      QuicNetworkHandle network) = 0;
  virtual void OnConnectionMigrationSuccess(MigrationCause migration_cause,
                                            QuicConnectionId connection_id) = 0;
};

using MigrationCallback =
    quiche::SingleUseCallback<void(QuicNetworkHandle, MigrationResult)>;
using StartProbingCallback = quiche::SingleUseCallback<void(ProbingResult)>;

// This class receives network change signals from the device and events
// reported by the connection, like path degrading and write error, to make
// decision about whether and how to migrate the connection to a different
// network or port.
class QUICHE_EXPORT QuicConnectionMigrationManager {
 public:
  // `path_context_factory` can be nullptr, in which case no migration will be
  // performed regardless of the migration `config`.
  QuicConnectionMigrationManager(
      QuicSpdyClientSessionWithMigration* absl_nonnull session,
      const quic::QuicClock* absl_nonnull clock,
      QuicNetworkHandle default_network, QuicNetworkHandle current_network,
      QuicPathContextFactory* absl_nullable path_context_factory,
      const QuicConnectionMigrationConfig& config);

  ~QuicConnectionMigrationManager();

  // Called when the platform detects a newly connected network. Migrates this
  // session to the newly connected network if the session has previously
  // attempted to migrate off the current network for various reasons but failed
  // because there was no alternate network available at the time.
  void OnNetworkConnected(QuicNetworkHandle network);

  // Called when the platform detects the given network to be disconnected.
  void OnNetworkDisconnected(QuicNetworkHandle disconnected_network);

  // Called when the platform chooses the given network as the default network.
  // Migrates this session to it if appropriate.
  void OnNetworkMadeDefault(QuicNetworkHandle new_network);

  // Maybe start migrating the session to a different port or a different
  // network.
  void OnPathDegrading();

  // Called by the session when write error occurs to attempt switching to a
  // different network.
  // Returns true to tell the caller to ignore this writer error.
  bool MaybeStartMigrateSessionOnWriteError(int error_code);

  // Called by the session when the handshake gets completed to attempt
  // switching to the platform's default network asynchronously if not on it
  // yet. |config| is the negotiated QUIC configuration.
  void OnHandshakeCompleted(const QuicConfig& negotiated_config);

  // Called by the session after receiving server's preferred address.
  void MaybeStartMigrateSessionToServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address);

  void OnMigrationFailure(QuicConnectionMigrationStatus status,
                          absl::string_view reason);

  // Called when migration alarm fires. If migration has not occurred
  // since alarm was set, closes session with error.
  void OnMigrationTimeout();
  // Called when there are pending callbacks to be executed.
  void RunPendingCallbacks();
  // Called when migrating to default network timer fires.
  void MaybeRetryMigrateBackToDefaultNetwork();

  // Called when probing alternative network for connection migration succeeds.
  void OnConnectionMigrationProbeSucceeded(
      std::unique_ptr<QuicPathValidationContext> path_context,
      quic::QuicTime start_time);
  // Called when probing a different port succeeds.
  void OnPortMigrationProbeSucceeded(
      std::unique_ptr<QuicPathValidationContext> path_context,
      quic::QuicTime start_time);
  // Called when probing the server's preferred address from a different port
  // succeeds.
  void OnServerPreferredAddressProbeSucceeded(
      std::unique_ptr<QuicPathValidationContext> path_context,
      quic::QuicTime start_time);

  // Called when any type of probing failed.
  void OnProbeFailed(std::unique_ptr<QuicPathValidationContext> path_context);

  void set_debug_visitor(
      QuicConnectionMigrationDebugVisitor* absl_nullable visitor) {
    debug_visitor_ = visitor;
  }

  const QuicConnectionMigrationConfig& config() const { return config_; }

  // Returns the network interface that is currently used to send packets.
  QuicNetworkHandle current_network() const { return current_network_; }

  // Returns the network interface that is picked as default by the platform.
  QuicNetworkHandle default_network() const { return default_network_; }

  bool migration_attempted() const { return migration_attempted_; }

  bool migration_successful() const { return migration_successful_; }

 private:
  friend class test::QuicConnectionMigrationManagerPeer;

  class PathContextCreationResultDelegateForImmediateMigration
      : public QuicPathContextFactory::CreationResultDelegate {
   public:
    // |migration_manager| should out live this instance.
    PathContextCreationResultDelegateForImmediateMigration(
        QuicConnectionMigrationManager* absl_nonnull migration_manager,
        bool close_session_on_error, MigrationCallback migration_callback);

    void OnCreationSucceeded(
        std::unique_ptr<QuicClientPathValidationContext> context) override;

    void OnCreationFailed(QuicNetworkHandle network,
                          absl::string_view error) override;

   private:
    QuicConnectionMigrationManager* absl_nonnull migration_manager_;
    const bool close_session_on_error_;
    MigrationCallback migration_callback_;
  };

  // A callback implementation for creating a path context object used for
  // probing.
  class PathContextCreationResultDelegateForProbing
      : public QuicPathContextFactory::CreationResultDelegate {
   public:
    // `migration_manager` should out live this instance.
    PathContextCreationResultDelegateForProbing(
        QuicConnectionMigrationManager* absl_nonnull migration_manager,
        StartProbingCallback probing_callback);

    void OnCreationSucceeded(
        std::unique_ptr<QuicClientPathValidationContext> context) override;

    void OnCreationFailed(QuicNetworkHandle network,
                          absl::string_view error) override;

   private:
    QuicConnectionMigrationManager* absl_nonnull migration_manager_;
    StartProbingCallback probing_callback_;
  };

  // Schedules a migration alarm to wait for a new network.
  void OnNoNewNetwork();

  // Called when there is only one possible working network: |network|, if any
  // error is encountered, this session will be closed.
  // When the migration succeeds:
  //  - If no longer on the default network, set timer to migrate back to the
  //    default network;
  //  - If now on the default network, cancel timer to migrate back to default
  //    network.
  void MigrateNetworkImmediately(QuicNetworkHandle network);

  void FinishMigrateNetworkImmediately(QuicNetworkHandle network,
                                       MigrationResult result);

  // Migrates session over to use |peer_address| and |network|.
  // If |network| is kInvalidNetworkHandle, default network is used. If
  // the migration fails and |close_session_on_error| is true, session will be
  // closed.
  void Migrate(QuicNetworkHandle network, QuicSocketAddress peer_address,
               bool close_session_on_error,
               MigrationCallback migration_callback);
  // Helper to finish session migration once the |path_context| is provided.
  void FinishMigrate(
      std::unique_ptr<QuicClientPathValidationContext> path_context,
      bool close_session_on_error, MigrationCallback callback);

  void StartMigrateBackToDefaultNetworkTimer(QuicTimeDelta delay);
  void CancelMigrateBackToDefaultNetworkTimer();

  void TryMigrateBackToDefaultNetwork(QuicTimeDelta next_try_timeout);

  void FinishTryMigrateBackToDefaultNetwork(QuicTimeDelta next_try_timeout,
                                            ProbingResult result);

  // Migration might happen asynchronously (async socket creation or no new
  // network).
  void StartMigrateSessionOnWriteError(QuicPacketWriter* writer);

  void FinishMigrateSessionOnWriteError(QuicNetworkHandle new_network,
                                        MigrationResult result);

  void MaybeProbeAndMigrateToAlternateNetworkOnPathDegrading();

  void StartProbing(StartProbingCallback probing_callback,
                    QuicNetworkHandle network,
                    const QuicSocketAddress& peer_address);
  void FinishStartProbing(
      StartProbingCallback probing_callback,
      std::unique_ptr<QuicClientPathValidationContext> path_context);

  bool MaybeCloseIdleSession(bool has_write_error,
                             ConnectionCloseBehavior close_behavior);

  void RunCallbackInNextLoop(quiche::SingleUseCallback<void()>);
  void RecordMetricsOnNetworkMadeDefault();
  void RecordMetricsOnNetworkDisconnected();
  void RecordHandshakeStatusOnMigrationSignal() const;
  void RecordProbeResultToHistogram(MigrationCause cause, bool success);
  void OnMigrationSuccess();
  void ResetMigrationCauseAndLogResult(QuicConnectionMigrationStatus status);

  QuicSpdyClientSessionWithMigration* absl_nonnull session_;
  QuicConnection* absl_nonnull connection_;
  const quic::QuicClock* absl_nonnull clock_;  // Unowned.
  // Stores the latest default network platform marks if migration is enabled.
  // Otherwise, stores the network interface that is currently used by the
  // connection (same as `current_network_`).
  QuicNetworkHandle default_network_;
  // Stores the network interface that is currently used by the connection.
  QuicNetworkHandle current_network_;
  // Nullptr if no migration is allowed.
  QuicPathContextFactory* absl_nullable path_context_factory_;
  // Not owned.
  QuicConnectionMigrationDebugVisitor* absl_nullable debug_visitor_ = nullptr;
  const QuicConnectionMigrationConfig config_;
  bool migration_disabled_ = false;

  // True when session migration has started from
  // `StartMigrateSessionOnWriteError`.
  bool pending_migrate_session_on_write_error_ = false;
  // True when a session migration starts from `MigrateNetworkImmediately`.
  bool pending_migrate_network_immediately_ = false;
  int retry_migrate_back_count_ = 0;
  MigrationCause current_migration_cause_ = MigrationCause::UNKNOWN_CAUSE;
  // True if migration is triggered, and there is no alternate network to
  // migrate to.
  bool wait_for_new_network_ = false;
  int current_migrations_to_non_default_network_on_write_error_ = 0;
  int current_migrations_to_non_default_network_on_path_degrading_ = 0;
  int current_migrations_to_different_port_on_path_degrading_ = 0;
  quic::QuicTime most_recent_path_degrading_timestamp_ = quic::QuicTime::Zero();
  quic::QuicTime most_recent_network_disconnected_timestamp_ =
      quic::QuicTime::Zero();
  int most_recent_write_error_;
  quic::QuicTime most_recent_write_error_timestamp_ = quic::QuicTime::Zero();
  bool migration_attempted_ = false;
  bool migration_successful_ = false;

  std::unique_ptr<QuicAlarm> migrate_back_to_default_timer_;
  std::unique_ptr<QuicAlarm> wait_for_migration_alarm_;
  std::unique_ptr<QuicAlarm> run_pending_callbacks_alarm_;
  std::list<quiche::SingleUseCallback<void()>> pending_callbacks_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_CONNECTION_MIGRATION_MANAGER_H_
