// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_connection_migration_manager.h"

#include <cstddef>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_client_session_with_migration.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_force_blockable_packet_writer.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_path_context_factory.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_client_stats.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_client_stats.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

namespace {
// Time to wait (in seconds) when no networks are available and
// migrating sessions need to wait for a new network to connect.
constexpr int kWaitTimeForNewNetworkSecs = 10;
// Minimum time to wait (in seconds) when retrying to migrate back to the
// default network.
constexpr int kMinRetryTimeForDefaultNetworkSecs = 1;

class WaitForMigrationDelegate : public QuicAlarm::Delegate {
 public:
  explicit WaitForMigrationDelegate(
      QuicConnectionMigrationManager* absl_nonnull migration_manager,
      QuicConnectionContext* absl_nullable context)
      : migration_manager_(migration_manager), context_(context) {}
  WaitForMigrationDelegate(const WaitForMigrationDelegate&) = delete;
  WaitForMigrationDelegate& operator=(const WaitForMigrationDelegate&) = delete;
  QuicConnectionContext* GetConnectionContext() override { return context_; }
  void OnAlarm() override { migration_manager_->OnMigrationTimeout(); }

 private:
  QuicConnectionMigrationManager* absl_nonnull migration_manager_;
  QuicConnectionContext* absl_nullable context_;
};

class MigrateBackToDefaultNetworkDelegate : public QuicAlarm::Delegate {
 public:
  MigrateBackToDefaultNetworkDelegate(
      QuicConnectionMigrationManager* absl_nonnull migration_manager,
      QuicConnectionContext* absl_nullable context)
      : migration_manager_(migration_manager), context_(context) {}
  MigrateBackToDefaultNetworkDelegate(
      const MigrateBackToDefaultNetworkDelegate&) = delete;
  MigrateBackToDefaultNetworkDelegate& operator=(
      const MigrateBackToDefaultNetworkDelegate&) = delete;
  QuicConnectionContext* GetConnectionContext() override { return context_; }
  void OnAlarm() override {
    migration_manager_->MaybeRetryMigrateBackToDefaultNetwork();
  }

 private:
  QuicConnectionMigrationManager* absl_nonnull migration_manager_;
  QuicConnectionContext* absl_nullable context_;
};

class RunPendingCallbackDelegate : public QuicAlarm::Delegate {
 public:
  RunPendingCallbackDelegate(
      QuicConnectionMigrationManager* absl_nonnull migration_manager,
      QuicConnectionContext* absl_nullable context)
      : migration_manager_(migration_manager), context_(context) {}
  RunPendingCallbackDelegate(const RunPendingCallbackDelegate&) = delete;
  RunPendingCallbackDelegate& operator=(const RunPendingCallbackDelegate&) =
      delete;
  QuicConnectionContext* GetConnectionContext() override { return context_; }
  void OnAlarm() override { migration_manager_->RunPendingCallbacks(); }

 private:
  QuicConnectionMigrationManager* absl_nonnull migration_manager_;
  QuicConnectionContext* absl_nullable context_;
};

// This class handles path validation results associated with connection
// migration which depends on probing.
class ConnectionMigrationValidationResultDelegate
    : public quic::QuicPathValidator::ResultDelegate {
 public:
  // `migration_manager` should out live this instance.
  explicit ConnectionMigrationValidationResultDelegate(
      QuicConnectionMigrationManager* absl_nonnull migration_manager)
      : migration_manager_(migration_manager) {}
  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context,
      quic::QuicTime start_time) override {
    migration_manager_->OnConnectionMigrationProbeSucceeded(std::move(context),
                                                            start_time);
  }
  void OnPathValidationFailure(
      std::unique_ptr<QuicPathValidationContext> context) override {
    migration_manager_->OnProbeFailed(std::move(context));
  }

 private:
  // `migration_manager_` should out live |this|.
  QuicConnectionMigrationManager* absl_nonnull migration_manager_;
};

// This class handles path validation results associated with port migration.
class PortMigrationValidationResultDelegate
    : public quic::QuicPathValidator::ResultDelegate {
 public:
  // `migration_manager` should out live this instance.
  explicit PortMigrationValidationResultDelegate(
      QuicConnectionMigrationManager* absl_nonnull migration_manager)
      : migration_manager_(migration_manager) {}
  void OnPathValidationSuccess(
      std::unique_ptr<quic::QuicPathValidationContext> context,
      quic::QuicTime start_time) override {
    migration_manager_->OnPortMigrationProbeSucceeded(std::move(context),
                                                      start_time);
  }
  void OnPathValidationFailure(
      std::unique_ptr<quic::QuicPathValidationContext> context) override {
    migration_manager_->OnProbeFailed(std::move(context));
  }

 private:
  // `migration_manager_` should out live |this|.
  QuicConnectionMigrationManager* absl_nonnull migration_manager_;
};

// This class handles path validation results associated with migrating to
// server preferred address.
class ServerPreferredAddressValidationResultDelegate
    : public quic::QuicPathValidator::ResultDelegate {
 public:
  // `migration_manager` should out live this instance.
  explicit ServerPreferredAddressValidationResultDelegate(
      QuicConnectionMigrationManager* absl_nonnull migration_manager)
      : migration_manager_(migration_manager) {}
  void OnPathValidationSuccess(
      std::unique_ptr<quic::QuicPathValidationContext> context,
      quic::QuicTime start_time) override {
    migration_manager_->OnServerPreferredAddressProbeSucceeded(
        std::move(context), start_time);
  }
  void OnPathValidationFailure(
      std::unique_ptr<quic::QuicPathValidationContext> context) override {
    migration_manager_->OnProbeFailed(std::move(context));
  }

 private:
  QuicConnectionMigrationManager* absl_nonnull migration_manager_;
};

std::string MigrationCauseToString(MigrationCause cause) {
  switch (cause) {
    case MigrationCause::UNKNOWN_CAUSE:
      return "Unknown";
    case MigrationCause::ON_NETWORK_CONNECTED:
      return "OnNetworkConnected";
    case MigrationCause::ON_NETWORK_DISCONNECTED:
      return "OnNetworkDisconnected";
    case MigrationCause::ON_WRITE_ERROR:
      return "OnWriteError";
    case MigrationCause::ON_NETWORK_MADE_DEFAULT:
      return "OnNetworkMadeDefault";
    case MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK:
      return "OnMigrateBackToDefaultNetwork";
    case MigrationCause::CHANGE_NETWORK_ON_PATH_DEGRADING:
      return "OnPathDegrading";
    case MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING:
      return "ChangePortOnPathDegrading";
    case MigrationCause::NEW_NETWORK_CONNECTED_POST_PATH_DEGRADING:
      return "NewNetworkConnectedPostPathDegrading";
    case MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE:
      return "OnServerPreferredAddressAvailable";
    default:
      QUICHE_NOTREACHED();
      break;
  }
  return "InvalidCause";
}

}  // namespace

QuicConnectionMigrationManager::QuicConnectionMigrationManager(
    QuicSpdyClientSessionWithMigration* absl_nonnull session,
    const quic::QuicClock* absl_nonnull clock,
    QuicNetworkHandle default_network, QuicNetworkHandle current_network,
    QuicPathContextFactory* absl_nullable path_context_factory,
    const QuicConnectionMigrationConfig& config)
    : session_(session),
      connection_(session->connection()),
      clock_(clock),
      default_network_(default_network),
      current_network_(current_network),
      path_context_factory_(path_context_factory),
      config_(config),
      migrate_back_to_default_timer_(connection_->alarm_factory()->CreateAlarm(
          new MigrateBackToDefaultNetworkDelegate(this,
                                                  connection_->context()))),
      wait_for_migration_alarm_(connection_->alarm_factory()->CreateAlarm(
          new WaitForMigrationDelegate(this, connection_->context()))),
      run_pending_callbacks_alarm_(connection_->alarm_factory()->CreateAlarm(
          new RunPendingCallbackDelegate(this, connection_->context()))) {
  QUICHE_BUG_IF(gquic_session_created_on_non_default_network,
                default_network_ != current_network_ &&
                    !session_->version().HasIetfQuicFrames());
  QUICHE_BUG_IF(inconsistent_migrate_session_config,
                config_.migrate_session_early &&
                    !config_.migrate_session_on_network_change)
      << "migrate_session_early must be false if "
         "migrate_session_on_network_change is false.";
}

QuicConnectionMigrationManager::~QuicConnectionMigrationManager() {
  wait_for_migration_alarm_->PermanentCancel();
  migrate_back_to_default_timer_->PermanentCancel();
  run_pending_callbacks_alarm_->PermanentCancel();
}

void QuicConnectionMigrationManager::OnNetworkConnected(
    QuicNetworkHandle network) {
  if (!session_->version().HasIetfQuicFrames()) {
    return;
  }
  if (connection_->IsPathDegrading()) {
    quic::QuicTimeDelta duration =
        clock_->Now() - most_recent_path_degrading_timestamp_;
    QUICHE_CLIENT_HISTOGRAM_TIMES(
        "QuicNetworkDegradingDurationTillConnected", duration,
        quic::QuicTimeDelta::FromMilliseconds(1),
        quic::QuicTimeDelta::FromSeconds(10 * 60), 50,
        "Time elapsed since last network degrading detected.");
  }
  if (debug_visitor_) {
    debug_visitor_->OnNetworkConnected(network);
  }
  if (!config_.migrate_session_on_network_change) {
    return;
  }
  // If there was no migration waiting for new network and the path is not
  // degrading, ignore this signal.
  if (!wait_for_new_network_ && !connection_->IsPathDegrading()) {
    return;
  }
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationAfterNetworkConnected(network);
  }
  if (connection_->IsPathDegrading()) {
    current_migration_cause_ =
        MigrationCause::NEW_NETWORK_CONNECTED_POST_PATH_DEGRADING;
  }
  if (wait_for_new_network_) {
    wait_for_new_network_ = false;
    if (debug_visitor_) {
      debug_visitor_->OnWaitingForNewNetworkSucceeded(network);
    }
    if (current_migration_cause_ == MigrationCause::ON_WRITE_ERROR) {
      ++current_migrations_to_non_default_network_on_write_error_;
    }
    // `wait_for_new_network_` is true, there was no working network previously.
    // `network` is now the only possible candidate, migrate immediately.
    MigrateNetworkImmediately(network);
  } else {
    // The connection is path degrading.
    QUICHE_DCHECK(connection_->IsPathDegrading());
    MaybeProbeAndMigrateToAlternateNetworkOnPathDegrading();
  }
}

void QuicConnectionMigrationManager::OnNetworkDisconnected(
    QuicNetworkHandle disconnected_network) {
  RecordMetricsOnNetworkDisconnected();
  if (debug_visitor_) {
    debug_visitor_->OnNetworkDisconnected(disconnected_network);
  }
  if (!session_->version().HasIetfQuicFrames()) {
    return;
  }
  if (!config_.migrate_session_on_network_change) {
    return;
  }
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationAfterNetworkDisconnected(
        disconnected_network);
  }
  // Stop probing the disconnected network if there is one.
  QuicPathValidationContext* context = connection_->GetPathValidationContext();
  if (context && context->network() == disconnected_network &&
      context->peer_address() == connection_->peer_address()) {
    connection_->CancelPathValidation();
  }

  if (disconnected_network == default_network_) {
    QUIC_DLOG(INFO) << "Default network: " << default_network_
                    << " is disconnected.";
    default_network_ = kInvalidNetworkHandle;
    current_migrations_to_non_default_network_on_write_error_ = 0;
  }
  // Ignore the signal if the current active network is not affected.
  if (current_network() != disconnected_network) {
    QUIC_DVLOG(1) << "Client's current default network is not affected by the "
                  << "disconnected one.";
    return;
  }
  if (pending_migrate_session_on_write_error_) {
    QUIC_DVLOG(1)
        << "Ignoring a network disconnection signal because a "
           "connection migration is happening due to a previous write error.";
    return;
  }
  if (config_.ignore_disconnect_signal_during_probing &&
      current_migration_cause_ == MigrationCause::ON_NETWORK_MADE_DEFAULT) {
    QUIC_DVLOG(1)
        << "Ignoring a network disconnection signal because a "
           "connection migration is happening on the default network.";
    return;
  }
  current_migration_cause_ = MigrationCause::ON_NETWORK_DISCONNECTED;
  RecordHandshakeStatusOnMigrationSignal();
  if (!session_->OneRttKeysAvailable()) {
    // Close the connection if handshake has not completed. Migration before
    // that is not allowed.
    // TODO(danzh): the current behavior aligns with Chrome. But according to
    // IETF spec, check handshake confirmed instead.
    session_->OnConnectionToBeClosedDueToMigrationError(
        current_migration_cause_,
        QUIC_CONNECTION_MIGRATION_HANDSHAKE_UNCONFIRMED);
    connection_->CloseConnection(
        QUIC_CONNECTION_MIGRATION_HANDSHAKE_UNCONFIRMED,
        "Network disconnected before handshake complete.",
        ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }
  // Attempt to find alternative network.
  QuicNetworkHandle new_network =
      session_->FindAlternateNetwork(disconnected_network);
  if (new_network == kInvalidNetworkHandle) {
    OnNoNewNetwork();
    return;
  }
  // Current network is being disconnected, migrate immediately to the
  // alternative network.
  MigrateNetworkImmediately(new_network);
}

void QuicConnectionMigrationManager::MigrateNetworkImmediately(
    QuicNetworkHandle network) {
  // There is no choice but to migrate to |network|. If any error encountered,
  // close the session. When migration succeeds:
  // - if no longer on the default network, start timer to migrate back;
  // - otherwise, it's brought to default network, cancel the running timer to
  //   migrate back.
  QUICHE_DCHECK(config_.migrate_session_on_network_change);
  if (MaybeCloseIdleSession(/*has_write_error=*/false,
                            ConnectionCloseBehavior::SILENT_CLOSE)) {
    return;
  }
  // Do not migrate if connection migration is disabled.
  if (migration_disabled_) {
    session_->OnConnectionToBeClosedDueToMigrationError(
        current_migration_cause_, QUIC_CONNECTION_MIGRATION_DISABLED_BY_CONFIG);
    connection_->CloseConnection(QUIC_CONNECTION_MIGRATION_DISABLED_BY_CONFIG,
                                 "Migration disabled by config",
                                 ConnectionCloseBehavior::SILENT_CLOSE);
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_DISABLED_BY_CONFIG,
        "Migration disabled by config");
    return;
  }
  if (network == current_network()) {
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_ALREADY_MIGRATED,
        "Already bound to new network");
    return;
  }
  // Cancel probing on |network| if there is any.
  QuicPathValidationContext* context = connection_->GetPathValidationContext();
  if (context && context->network() == network &&
      context->peer_address() == connection_->peer_address()) {
    connection_->CancelPathValidation();
  }
  pending_migrate_network_immediately_ = true;
  Migrate(network, connection_->peer_address(),
          /*close_session_on_error=*/true,
          [this](QuicNetworkHandle network, MigrationResult result) {
            FinishMigrateNetworkImmediately(network, result);
          });
}

QuicConnectionMigrationManager::
    PathContextCreationResultDelegateForImmediateMigration::
        PathContextCreationResultDelegateForImmediateMigration(
            QuicConnectionMigrationManager* absl_nonnull migration_manager,
            bool close_session_on_error, MigrationCallback migration_callback)
    : migration_manager_(migration_manager),
      close_session_on_error_(close_session_on_error),
      migration_callback_(std::move(migration_callback)) {}

void QuicConnectionMigrationManager::
    PathContextCreationResultDelegateForImmediateMigration::OnCreationSucceeded(
        std::unique_ptr<QuicClientPathValidationContext> context) {
  migration_manager_->FinishMigrate(std::move(context), close_session_on_error_,
                                    std::move(migration_callback_));
}

void QuicConnectionMigrationManager::
    PathContextCreationResultDelegateForImmediateMigration::OnCreationFailed(
        QuicNetworkHandle network, absl::string_view error) {
  migration_manager_->session_->writer()->ForceWriteBlocked(false);
  std::move(migration_callback_)(network, MigrationResult::FAILURE);
  if (close_session_on_error_) {
    migration_manager_->session_->OnConnectionToBeClosedDueToMigrationError(
        migration_manager_->current_migration_cause_,
        QUIC_CONNECTION_MIGRATION_INTERNAL_ERROR);
    migration_manager_->session_->connection()->CloseConnection(
        QUIC_CONNECTION_MIGRATION_INTERNAL_ERROR,
        "Failed to create a path context",
        ConnectionCloseBehavior::SILENT_CLOSE);
  }
  migration_manager_->OnMigrationFailure(
      QuicConnectionMigrationStatus::MIGRATION_STATUS_INTERNAL_ERROR, error);
}

QuicConnectionMigrationManager::PathContextCreationResultDelegateForProbing::
    PathContextCreationResultDelegateForProbing(
        QuicConnectionMigrationManager* absl_nonnull migration_manager,
        StartProbingCallback probing_callback)
    : migration_manager_(migration_manager),
      probing_callback_(std::move(probing_callback)) {}

void QuicConnectionMigrationManager::
    PathContextCreationResultDelegateForProbing::OnCreationSucceeded(
        std::unique_ptr<QuicClientPathValidationContext> context) {
  migration_manager_->FinishStartProbing(std::move(probing_callback_),
                                         std::move(context));
}

void QuicConnectionMigrationManager::
    PathContextCreationResultDelegateForProbing::OnCreationFailed(
        QuicNetworkHandle /*network*/, absl::string_view error) {
  migration_manager_->OnMigrationFailure(
      QuicConnectionMigrationStatus::MIGRATION_STATUS_INTERNAL_ERROR, error);
  if (probing_callback_) {
    std::move(probing_callback_)(ProbingResult::INTERNAL_ERROR);
  }
}

void QuicConnectionMigrationManager::Migrate(
    QuicNetworkHandle network, QuicSocketAddress peer_address,
    bool close_session_on_error, MigrationCallback migration_callback) {
  migration_attempted_ = true;
  migration_successful_ = false;
  if (!path_context_factory_) {
    std::move(migration_callback)(network, MigrationResult::FAILURE);
    return;
  }
  if (network != kInvalidNetworkHandle) {
    // This is a migration attempt from connection migration.
    session_->ResetNonMigratableStreams();
    if (!config_.migrate_idle_session && !session_->HasActiveRequestStreams()) {
      std::move(migration_callback)(network, MigrationResult::FAILURE);
      // If idle sessions can not be migrated, close the session if needed.
      if (close_session_on_error) {
        session_->OnConnectionToBeClosedDueToMigrationError(
            current_migration_cause_,
            QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS);
        connection_->CloseConnection(
            QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS,
            "Migrating idle session is disabled.",
            ConnectionCloseBehavior::SILENT_CLOSE);
      }
      return;
    }
  } else {
    // TODO(b/430345640): remove the if condition if the historgram is not hit
    // at all in production.
    QUIC_CLIENT_HISTOGRAM_BOOL(
        "QuicSession.MigratingToInvalidNetwork", true,
        "Connection is migrating with an invalid network handle.");
  }
  QUIC_DVLOG(1) << "Force blocking the current packet writer";
  session_->writer()->ForceWriteBlocked(true);
  if (config_.disable_blackhole_detection_on_immediate_migrate) {
    // Turn off the black hole detector since the writer is blocked.
    // Blackhole will be re-enabled once a packet is sent again.
    connection_->blackhole_detector().StopDetection(false);
  }
  path_context_factory_->CreatePathValidationContext(
      network, peer_address,
      std::make_unique<PathContextCreationResultDelegateForImmediateMigration>(
          this, close_session_on_error, std::move(migration_callback)));
}

void QuicConnectionMigrationManager::FinishMigrateNetworkImmediately(
    QuicNetworkHandle network, MigrationResult result) {
  pending_migrate_network_immediately_ = false;
  if (result == MigrationResult::FAILURE) {
    QUIC_DVLOG(1) << "Failed to migrate network immediately";
    return;
  }
  if (network == default_network_) {
    CancelMigrateBackToDefaultNetworkTimer();
    return;
  }
  // We are forced to migrate to |network|, probably |default_network_| is
  // not working, start to migrate back to default network after 1 secs.
  StartMigrateBackToDefaultNetworkTimer(
      QuicTimeDelta::FromSeconds(kMinRetryTimeForDefaultNetworkSecs));
}

void QuicConnectionMigrationManager::FinishMigrate(
    std::unique_ptr<QuicClientPathValidationContext> path_context,
    bool close_session_on_error, MigrationCallback callback) {
  // Migrate to the new socket.
  MigrationCause current_migration_cause = current_migration_cause_;
  QuicNetworkHandle network = path_context->network();
  if (!session_->MigrateToNewPath(std::move(path_context))) {
    session_->writer()->ForceWriteBlocked(false);
    std::move(callback)(network, MigrationResult::FAILURE);
    if (close_session_on_error) {
      session_->OnConnectionToBeClosedDueToMigrationError(
          current_migration_cause, QUIC_CONNECTION_MIGRATION_INTERNAL_ERROR);
      connection_->CloseConnection(QUIC_CONNECTION_MIGRATION_INTERNAL_ERROR,
                                   "Session failed to migrate to new path.",
                                   ConnectionCloseBehavior::SILENT_CLOSE);
    }
    return;
  }
  current_network_ = network;
  wait_for_migration_alarm_->Cancel();
  migration_successful_ = true;
  OnMigrationSuccess();
  std::move(callback)(network, MigrationResult::SUCCESS);
}

void QuicConnectionMigrationManager::OnNoNewNetwork() {
  QUICHE_DCHECK(session_->OneRttKeysAvailable());
  wait_for_new_network_ = true;
  if (debug_visitor_) {
    debug_visitor_->OnWaitingForNewNetworkToMigrate();
  }
  QUIC_DVLOG(1) << "Force blocking the packet writer while waiting for new "
                   "netowrk for migraion cause "
                << MigrationCauseToString(current_migration_cause_);
  // Force blocking the packet writer to avoid any writes since there is no
  // alternate network available.
  session_->writer()->ForceWriteBlocked(true);
  if (config_.disable_blackhole_detection_on_immediate_migrate) {
    // Turn off the black hole detector since the writer is blocked.
    // Blackhole will be re-enabled once a packet is sent again.
    connection_->blackhole_detector().StopDetection(false);
  }
  session_->OnNoNewNetworkForMigration();
  // Set an alarm to close the session if not being able to migrate to a new
  // network soon.
  if (!wait_for_migration_alarm_->IsSet()) {
    wait_for_migration_alarm_->Set(
        clock_->ApproximateNow() +
        QuicTimeDelta::FromSeconds(kWaitTimeForNewNetworkSecs));
  }
}

void QuicConnectionMigrationManager::OnMigrationTimeout() {
  if (debug_visitor_) {
    debug_visitor_->OnWaitForNetworkFailed();
  }
  MigrationCause current_migration_cause = current_migration_cause_;
  // |current_migration_cause_| will be reset after logging.
  ResetMigrationCauseAndLogResult(
      QuicConnectionMigrationStatus::MIGRATION_STATUS_TIMEOUT);
  session_->OnConnectionToBeClosedDueToMigrationError(
      current_migration_cause, QUIC_CONNECTION_MIGRATION_NO_NEW_NETWORK);
  connection_->CloseConnection(
      QUIC_CONNECTION_MIGRATION_NO_NEW_NETWORK,
      absl::StrFormat("Migration for cause %s timed out",
                      MigrationCauseToString(current_migration_cause)),
      ConnectionCloseBehavior::SILENT_CLOSE);
}

void QuicConnectionMigrationManager::StartMigrateBackToDefaultNetworkTimer(
    QuicTimeDelta delay) {
  if (current_migration_cause_ != MigrationCause::ON_NETWORK_MADE_DEFAULT) {
    current_migration_cause_ =
        MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK;
  }
  CancelMigrateBackToDefaultNetworkTimer();
  // Try migrate back to default network after |delay|.
  migrate_back_to_default_timer_->Set(clock_->ApproximateNow() + delay);
}

void QuicConnectionMigrationManager::CancelMigrateBackToDefaultNetworkTimer() {
  retry_migrate_back_count_ = 0;
  migrate_back_to_default_timer_->Cancel();
}

bool QuicConnectionMigrationManager::MaybeStartMigrateSessionOnWriteError(
    int error_code) {
  if (!session_->version().HasIetfQuicFrames()) {
    return false;
  }
  QuicClientSparseHistogram("QuicSession.WriteError", -error_code);
  if (session_->OneRttKeysAvailable()) {
    QuicClientSparseHistogram("QuicSession.WriteError.HandshakeConfirmed",
                              -error_code);
  }
  // Proxied sessions cannot presently encounter write errors, but in case that
  // changes, those sessions should not attempt migration when such an error
  // occurs. The underlying connection to the proxy server may still migrate.
  if (session_->IsSessionProxied()) {
    return false;
  }
  std::optional<int> msg_too_big_error =
      connection_->writer()->MessageTooBigErrorCode();
  if ((msg_too_big_error.has_value() && error_code == *msg_too_big_error) ||
      !path_context_factory_ || !config_.migrate_session_on_network_change ||
      !session_->OneRttKeysAvailable()) {
    return false;
  }
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationAfterWriteError(current_network_);
  }
  most_recent_write_error_timestamp_ = clock_->ApproximateNow();
  most_recent_write_error_ = error_code;
  // Migrate the session onto a new network in the next event loop.
  RunCallbackInNextLoop([this, writer = connection_->writer()]() {
    StartMigrateSessionOnWriteError(writer);
  });
  return true;
}

void QuicConnectionMigrationManager::StartMigrateSessionOnWriteError(
    quic::QuicPacketWriter* writer) {
  QUICHE_DCHECK(config_.migrate_session_on_network_change);
  // If `writer` is no longer actively in use, or a parallel connection
  // migration has started from MigrateNetworkImmediately, abort this migration
  // attempt.
  if (writer != connection_->writer() || pending_migrate_network_immediately_) {
    return;
  }
  current_migration_cause_ = MigrationCause::ON_WRITE_ERROR;
  RecordHandshakeStatusOnMigrationSignal();
  if (MaybeCloseIdleSession(/*has_write_error=*/true,
                            ConnectionCloseBehavior::SILENT_CLOSE)) {
    return;
  }
  // Do not migrate if connection migration is disabled.
  if (migration_disabled_) {
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_DISABLED_BY_CONFIG,
        "Migration disabled by config");
    // Close the connection since migration was disabled. Do not cause a
    // connection close packet to be sent since socket may be borked.
    connection_->CloseConnection(QUIC_CONNECTION_MIGRATION_DISABLED_BY_CONFIG,
                                 "Unrecoverable write error",
                                 quic::ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }
  QuicNetworkHandle new_network =
      session_->FindAlternateNetwork(current_network());
  if (new_network == kInvalidNetworkHandle) {
    OnNoNewNetwork();
    return;
  }
  if (current_network() == default_network_) {
    if (current_migrations_to_non_default_network_on_write_error_ >=
        config_.max_migrations_to_non_default_network_on_write_error) {
      OnMigrationFailure(QuicConnectionMigrationStatus::
                             MIGRATION_STATUS_ON_WRITE_ERROR_DISABLED,
                         "Exceeds maximum number of migrations on write error");
      // Close the connection if migration failed. Do not cause a
      // connection close packet to be sent since socket may be borked.
      connection_->CloseConnection(
          QUIC_PACKET_WRITE_ERROR,
          "Too many migrations for write error for the same network",
          ConnectionCloseBehavior::SILENT_CLOSE);
      return;
    }
    ++current_migrations_to_non_default_network_on_write_error_;
  }
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationStartingAfterEvent("WriteError");
  }
  pending_migrate_session_on_write_error_ = true;
  Migrate(new_network, connection_->peer_address(),
          /*close_session_on_error=*/false,
          [this](QuicNetworkHandle new_network, MigrationResult rv) {
            FinishMigrateSessionOnWriteError(new_network, rv);
          });
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationStarted();
  }
}

void QuicConnectionMigrationManager::FinishMigrateSessionOnWriteError(
    QuicNetworkHandle new_network, MigrationResult result) {
  pending_migrate_session_on_write_error_ = false;
  if (result == MigrationResult::FAILURE) {
    // Close the connection if migration failed. Do not cause a
    // connection close packet to be sent since socket may be borked.
    connection_->CloseConnection(QUIC_PACKET_WRITE_ERROR,
                                 "Write and subsequent migration failed",
                                 ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }
  if (new_network != default_network_) {
    StartMigrateBackToDefaultNetworkTimer(
        QuicTimeDelta::FromSeconds(kMinRetryTimeForDefaultNetworkSecs));
  } else {
    CancelMigrateBackToDefaultNetworkTimer();
  }
}

void QuicConnectionMigrationManager::RunCallbackInNextLoop(
    quiche::SingleUseCallback<void()> callback) {
  if (callback == nullptr) {
    return;
  }
  pending_callbacks_.push_back(std::move(callback));
  if (pending_callbacks_.size() == 1u) {
    run_pending_callbacks_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnectionMigrationManager::RunPendingCallbacks() {
  std::list<quiche::SingleUseCallback<void()>> pending_callbacks =
      std::move(pending_callbacks_);
  while (!pending_callbacks.empty()) {
    std::move(pending_callbacks.front())();
    pending_callbacks.pop_front();
  }
}

void QuicConnectionMigrationManager::OnPathDegrading() {
  if (!session_->version().HasIetfQuicFrames()) {
    return;
  }
  if (!most_recent_path_degrading_timestamp_.IsInitialized()) {
    most_recent_path_degrading_timestamp_ = clock_->ApproximateNow();
  }
  // Proxied sessions should not attempt migration when the path degrades, as
  // there is nowhere for such a session to migrate to. If the degradation is
  // due to degradation of the underlying session, then that session may attempt
  // migration.
  if (session_->IsSessionProxied()) {
    return;
  }
  if (!path_context_factory_ || connection_->multi_port_stats()) {
    return;
  }
  const bool migrate_session_early = config_.migrate_session_early &&
                                     config_.migrate_session_on_network_change;
  current_migration_cause_ =
      (config_.allow_port_migration && !migrate_session_early)
          ? MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING
          : MigrationCause::CHANGE_NETWORK_ON_PATH_DEGRADING;
  RecordHandshakeStatusOnMigrationSignal();
  if (!connection_->IsHandshakeConfirmed()) {
    OnMigrationFailure(
        QuicConnectionMigrationStatus::
            MIGRATION_STATUS_PATH_DEGRADING_BEFORE_HANDSHAKE_CONFIRMED,
        "Path degrading before handshake confirmed");
    return;
  }
  if (migration_disabled_) {
    QUICHE_DVLOG(1)
        << "Client disables probing network with connection migration "
        << "disabled by config";
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_DISABLED_BY_CONFIG,
        "Migration disabled by config");
    return;
  }
  if (current_migration_cause_ ==
      MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING) {
    if (current_migrations_to_different_port_on_path_degrading_ >=
        config_.max_port_migrations_per_session) {
      // Note that Chrome implementation hasn't limited the number of port
      // migrations if config_.migrate_session_on_network_change is true and
      // config_.migrate_session_early is false.
      OnMigrationFailure(
          QuicConnectionMigrationStatus::MIGRATION_STATUS_TOO_MANY_CHANGES,
          "Too many changes");
      return;
    }

    QUICHE_DLOG(INFO) << "Start probing a different port on path degrading.";
    if (debug_visitor_) {
      debug_visitor_->OnPortMigrationStarting();
    }
    // Probe a different port, session will migrate to the probed port on
    // success.
    StartProbing(nullptr, default_network_, connection_->peer_address());
    if (debug_visitor_) {
      debug_visitor_->OnPortMigrationStarted();
    }
    return;
  }
  if (!migrate_session_early) {
    OnMigrationFailure(QuicConnectionMigrationStatus::
                           MIGRATION_STATUS_PATH_DEGRADING_NOT_ENABLED,
                       "Migration on path degrading not enabled");
    return;
  }
  MaybeProbeAndMigrateToAlternateNetworkOnPathDegrading();
}

void QuicConnectionMigrationManager::
    MaybeProbeAndMigrateToAlternateNetworkOnPathDegrading() {
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationStartingAfterEvent("PathDegrading");
  }
  if (current_network() == default_network_ &&
      current_migrations_to_non_default_network_on_path_degrading_ >=
          config_.max_migrations_to_non_default_network_on_path_degrading) {
    OnMigrationFailure(
        QuicConnectionMigrationStatus::
            MIGRATION_STATUS_ON_PATH_DEGRADING_DISABLED,
        "Exceeds maximum number of migrations on path degrading");
    return;
  }
  QuicNetworkHandle alternate_network =
      session_->FindAlternateNetwork(current_network());
  if (alternate_network == kInvalidNetworkHandle) {
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_NO_ALTERNATE_NETWORK,
        "No alternative network on path degrading");
    return;
  }
  if (MaybeCloseIdleSession(
          /*has_write_error=*/false,
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET)) {
    return;
  }
  // Probe the alternative network, session will migrate to the probed
  // network and decide whether it wants to migrate back to the default
  // network on success. Null is passed in for `start_probing_callback` as the
  // return value of StartProbing is not needed.
  StartProbing(nullptr, alternate_network, connection_->peer_address());
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationStarted();
  }
}

void QuicConnectionMigrationManager::MaybeRetryMigrateBackToDefaultNetwork() {
  // Exponentially backoff the retry timeout.
  QuicTimeDelta retry_migrate_back_timeout =
      QuicTimeDelta::FromSeconds(UINT64_C(1) << retry_migrate_back_count_);
  if (pending_migrate_session_on_write_error_ ||
      pending_migrate_network_immediately_) {
    // Another more pressing migration (without probing) is in progress, which
    // might migrate the session back to the default network. Wait for it to
    // finish before retrying to migrate back to default network with probing.
    StartMigrateBackToDefaultNetworkTimer(QuicTimeDelta::FromSeconds(0));
    return;
  }
  if (retry_migrate_back_timeout > config_.max_time_on_non_default_network) {
    // Mark session as going away to accept no more streams.
    session_->StartDraining();
    return;
  }
  TryMigrateBackToDefaultNetwork(retry_migrate_back_timeout);
}

void QuicConnectionMigrationManager::TryMigrateBackToDefaultNetwork(
    QuicTimeDelta next_try_timeout) {
  if (default_network_ == kInvalidNetworkHandle) {
    QUICHE_DVLOG(1) << "Default network is not connected";
    return;
  }
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationBackToDefaultNetwork(
        retry_migrate_back_count_);
  }
  if (!path_context_factory_) {
    FinishTryMigrateBackToDefaultNetwork(
        next_try_timeout, ProbingResult::DISABLED_WITH_IDLE_SESSION);
    return;
  }
  if (MaybeCloseIdleSession(
          /*has_write_error=*/false,
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET)) {
    FinishTryMigrateBackToDefaultNetwork(
        next_try_timeout, ProbingResult::DISABLED_WITH_IDLE_SESSION);
    return;
  }
  if (migration_disabled_) {
    QUIC_DVLOG(1)
        << "Client disables probing network with connection migration "
        << "disabled by config";
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_DISABLED_BY_CONFIG,
        "Migration disabled by config");
    FinishTryMigrateBackToDefaultNetwork(next_try_timeout,
                                         ProbingResult::DISABLED_BY_CONFIG);
    return;
  }
  // Start probe default network immediately, if this observer is probing
  // the same network, this will be a no-op. Otherwise, previous probe
  // will be cancelled and it starts to probe |default_network_|
  // immediately.
  StartProbing(
      [this, next_try_timeout](ProbingResult rv) {
        FinishTryMigrateBackToDefaultNetwork(next_try_timeout, rv);
      },
      default_network_, connection_->peer_address());
}

void QuicConnectionMigrationManager::FinishTryMigrateBackToDefaultNetwork(
    QuicTimeDelta next_try_timeout, ProbingResult result) {
  if (result != ProbingResult::PENDING) {
    // Session is not allowed to migrate, mark session as going away, cancel
    // migrate back to default timer.
    session_->StartDraining();
    CancelMigrateBackToDefaultNetworkTimer();
    return;
  }
  ++retry_migrate_back_count_;
  migrate_back_to_default_timer_->Set(clock_->ApproximateNow() +
                                      next_try_timeout);
}

void QuicConnectionMigrationManager::StartProbing(
    StartProbingCallback probing_callback, QuicNetworkHandle network,
    const quic::QuicSocketAddress& peer_address) {
  // Check if probing manager is probing the same path.
  QuicPathValidationContext* existing_context =
      connection_->GetPathValidationContext();
  if (existing_context && existing_context->network() == network &&
      existing_context->peer_address() == peer_address) {
    if (probing_callback) {
      QUIC_DVLOG(1) << "On-going probing of peer address " << peer_address
                    << " on network " << network << " hasn't finished.";
      std::move(probing_callback)(ProbingResult::DISABLED_BY_CONFIG);
    }
    return;
  }
  path_context_factory_->CreatePathValidationContext(
      network, peer_address,
      std::make_unique<PathContextCreationResultDelegateForProbing>(
          this, std::move(probing_callback)));
}

void QuicConnectionMigrationManager::FinishStartProbing(
    StartProbingCallback probing_callback,
    std::unique_ptr<QuicClientPathValidationContext> path_context) {
  session_->PrepareForProbingOnPath(*path_context);
  switch (current_migration_cause_) {
    case MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING:
      connection_->ValidatePath(
          std::move(path_context),
          std::make_unique<PortMigrationValidationResultDelegate>(this),
          quic::PathValidationReason::kPortMigration);
      break;
    case MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE:
      connection_->ValidatePath(
          std::move(path_context),
          std::make_unique<ServerPreferredAddressValidationResultDelegate>(
              this),
          quic::PathValidationReason::kServerPreferredAddressMigration);
      break;
    default:
      connection_->ValidatePath(
          std::move(path_context),
          std::make_unique<ConnectionMigrationValidationResultDelegate>(this),
          quic::PathValidationReason::kConnectionMigration);
      break;
  }
  if (probing_callback) {
    std::move(probing_callback)(ProbingResult::PENDING);
  }
}

void QuicConnectionMigrationManager::OnProbeFailed(
    std::unique_ptr<QuicPathValidationContext> path_context) {
  connection_->OnPathValidationFailureAtClient(
      /*is_multi_port=*/false, *path_context);
  QuicNetworkHandle network = path_context->network();
  if (debug_visitor_) {
    debug_visitor_->OnProbeResult(network, connection_->peer_address(),
                                  /*success=*/false);
  }
  RecordProbeResultToHistogram(current_migration_cause_, false);
  QuicPathValidationContext* context = connection_->GetPathValidationContext();
  if (!context) {
    return;
  }
  if (context->network() == network &&
      context->peer_address() == connection_->peer_address()) {
    connection_->CancelPathValidation();
  }
  if (network != kInvalidNetworkHandle) {
    // Probing failure can be ignored.
    QUICHE_DVLOG(1) << "Connectivity probing failed on <network: " << network
                    << ", peer_address: "
                    << connection_->peer_address().ToString() << ">.";
    QUICHE_DVLOG_IF(
        1, network == default_network_ && current_network() != default_network_)
        << "Client probing failed on the default network, still using "
           "non-default network.";
  }
}

void QuicConnectionMigrationManager::OnConnectionMigrationProbeSucceeded(
    std::unique_ptr<QuicPathValidationContext> path_context,
    quic::QuicTime /*start_time*/) {
  QuicNetworkHandle network = path_context->network();
  if (debug_visitor_) {
    debug_visitor_->OnProbeResult(network, connection_->peer_address(),
                                  /*success*/ true);
  }
  RecordProbeResultToHistogram(current_migration_cause_, true);
  // Close streams that are not migratable to the probed |network|.
  session_->ResetNonMigratableStreams();
  if (MaybeCloseIdleSession(
          /*has_write_error=*/false,
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET)) {
    return;
  }
  // Migrate to the probed socket immediately.
  if (!session_->MigrateToNewPath(
          std::unique_ptr<QuicClientPathValidationContext>(
              static_cast<QuicClientPathValidationContext*>(
                  path_context.release())))) {
    if (debug_visitor_) {
      debug_visitor_->OnConnectionMigrationFailedAfterProbe();
    }
    return;
  }
  OnMigrationSuccess();
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationSucceededAfterProbe(network);
  }
  current_network_ = network;
  if (network == default_network_) {
    QUIC_DVLOG(1) << "Client successfully migrated to default network: "
                  << default_network_;
    CancelMigrateBackToDefaultNetworkTimer();
    return;
  }
  QUIC_DVLOG(1) << "Client successfully got off default network after "
                << "successful probing network: " << network << ".";
  ++current_migrations_to_non_default_network_on_path_degrading_;
  if (!migrate_back_to_default_timer_->IsSet()) {
    current_migration_cause_ =
        MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK;
    // Session gets off the |default_network|, stay on |network| for now but
    // try to migrate back to default network after 1 second.
    StartMigrateBackToDefaultNetworkTimer(
        QuicTimeDelta::FromSeconds(kMinRetryTimeForDefaultNetworkSecs));
  }
}

void QuicConnectionMigrationManager::OnPortMigrationProbeSucceeded(
    std::unique_ptr<QuicPathValidationContext> path_context,
    quic::QuicTime /*start_time*/) {
  QuicNetworkHandle network = path_context->network();
  if (debug_visitor_) {
    debug_visitor_->OnProbeResult(network, connection_->peer_address(),
                                  /*success=*/true);
  }
  RecordProbeResultToHistogram(current_migration_cause_, true);
  if (MaybeCloseIdleSession(
          /*has_write_error=*/false,
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET)) {
    return;
  }
  // Migrate to the probed socket immediately.
  if (!session_->MigrateToNewPath(
          std::unique_ptr<QuicClientPathValidationContext>(
              static_cast<QuicClientPathValidationContext*>(
                  path_context.release())))) {
    if (debug_visitor_) {
      debug_visitor_->OnConnectionMigrationFailedAfterProbe();
    }
    return;
  }
  ++current_migrations_to_different_port_on_path_degrading_;
  OnMigrationSuccess();
}

void QuicConnectionMigrationManager::OnServerPreferredAddressProbeSucceeded(
    std::unique_ptr<QuicPathValidationContext> path_context,
    quic::QuicTime /*start_time*/) {
  QuicNetworkHandle network = path_context->network();
  if (debug_visitor_) {
    debug_visitor_->OnProbeResult(network, connection_->peer_address(),
                                  /*success*/ true);
  }
  RecordProbeResultToHistogram(current_migration_cause_, true);
  connection_->mutable_stats().server_preferred_address_validated = true;
  // Migrate to the probed socket immediately.
  if (!session_->MigrateToNewPath(
          std::unique_ptr<QuicClientPathValidationContext>(
              static_cast<QuicClientPathValidationContext*>(
                  path_context.release())))) {
    if (debug_visitor_) {
      debug_visitor_->OnConnectionMigrationFailedAfterProbe();
    }
    return;
  }
  OnMigrationSuccess();
}

void QuicConnectionMigrationManager::
    MaybeStartMigrateSessionToServerPreferredAddress(
        const quic::QuicSocketAddress& server_preferred_address) {
  // If this is a proxied connection, we cannot perform any migration, so
  // ignore the server preferred address.
  if (session_->IsSessionProxied()) {
    if (debug_visitor_) {
      debug_visitor_->OnConnectionMigrationFailed(
          MigrationCause::UNKNOWN_CAUSE, connection_->connection_id(),
          "Ignored server preferred address received via proxied connection");
    }
    return;
  }
  if (!config_.allow_server_preferred_address) {
    return;
  }
  current_migration_cause_ =
      MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE;
  if (!path_context_factory_) {
    return;
  }
  if (debug_visitor_) {
    debug_visitor_->OnProbingServerPreferredAddressStarting();
  }
  StartProbing(nullptr, default_network_, server_preferred_address);
  if (debug_visitor_) {
    debug_visitor_->OnProbingServerPreferredAddressStarted();
  }
}

void QuicConnectionMigrationManager::OnNetworkMadeDefault(
    QuicNetworkHandle new_network) {
  if (!session_->version().HasIetfQuicFrames()) {
    return;
  }
  RecordMetricsOnNetworkMadeDefault();
  if (debug_visitor_) {
    debug_visitor_->OnNetworkMadeDefault(new_network);
  }
  if (!config_.migrate_session_on_network_change ||
      session_->IsSessionProxied()) {
    return;
  }
  QUICHE_DCHECK_NE(kInvalidNetworkHandle, new_network);
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationAfterNewDefaultNetwork(new_network);
  }
  if (new_network == default_network_) {
    return;
  }
  QUICHE_DVLOG(1) << "Network: " << new_network
                  << " becomes default, old default: " << default_network_
                  << " current_network " << current_network();
  default_network_ = new_network;
  current_migration_cause_ = MigrationCause::ON_NETWORK_MADE_DEFAULT;
  current_migrations_to_non_default_network_on_write_error_ = 0;
  current_migrations_to_non_default_network_on_path_degrading_ = 0;
  // Simply cancel the timer to migrate back to the default network if session
  // is already on the default network.
  if (current_network() == new_network) {
    CancelMigrateBackToDefaultNetworkTimer();
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_ALREADY_MIGRATED,
        "Already migrated on the new network");
    return;
  }
  RecordHandshakeStatusOnMigrationSignal();
  // Stay on the current network. Try to migrate back to default network
  // without any delay, which will start probing the new default network and
  // migrate to the new network immediately on success.
  StartMigrateBackToDefaultNetworkTimer(QuicTimeDelta::Zero());
}

void QuicConnectionMigrationManager::RecordMetricsOnNetworkMadeDefault() {
  if (most_recent_path_degrading_timestamp_.IsInitialized()) {
    if (most_recent_network_disconnected_timestamp_.IsInitialized()) {
      // NetworkDisconnected happens before NetworkMadeDefault, the platform
      // is dropping WiFi.
      QuicTime now = clock_->ApproximateNow();
      QuicTimeDelta disconnection_duration =
          now - most_recent_network_disconnected_timestamp_;
      QuicTimeDelta degrading_duration =
          now - most_recent_path_degrading_timestamp_;
      QUIC_CLIENT_HISTOGRAM_TIMES("QuicNetworkDisconnectionDuration",
                                  disconnection_duration,
                                  QuicTimeDelta::FromMilliseconds(1),
                                  QuicTimeDelta::FromSeconds(10 * 60), 100, "");
      QUIC_CLIENT_HISTOGRAM_TIMES(
          "QuicNetworkDegradingDurationTillNewNetworkMadeDefault",
          degrading_duration, QuicTimeDelta::FromMilliseconds(1),
          QuicTimeDelta::FromSeconds(10 * 60), 100, "");
      most_recent_network_disconnected_timestamp_ = QuicTime::Zero();
    }
    most_recent_path_degrading_timestamp_ = QuicTime::Zero();
  }
}

void QuicConnectionMigrationManager::RecordMetricsOnNetworkDisconnected() {
  most_recent_network_disconnected_timestamp_ = clock_->ApproximateNow();
  if (most_recent_path_degrading_timestamp_.IsInitialized()) {
    QuicTimeDelta degrading_duration =
        most_recent_network_disconnected_timestamp_ -
        most_recent_path_degrading_timestamp_;
    QUIC_CLIENT_HISTOGRAM_TIMES("QuicNetworkDegradingDurationTillDisconnected",
                                degrading_duration,
                                QuicTimeDelta::FromMilliseconds(1),
                                QuicTimeDelta::FromSeconds(10 * 60), 100, "");
  }
  if (most_recent_write_error_timestamp_.IsInitialized()) {
    QuicTimeDelta write_error_to_disconnection_gap =
        most_recent_network_disconnected_timestamp_ -
        most_recent_write_error_timestamp_;
    QUIC_CLIENT_HISTOGRAM_TIMES(
        "QuicNetworkGapBetweenWriteErrorAndDisconnection",
        write_error_to_disconnection_gap, QuicTimeDelta::FromMilliseconds(1),
        QuicTimeDelta::FromSeconds(10 * 60), 100, "");
    QuicClientSparseHistogram("QuicSession.WriteError.NetworkDisconnected",
                              -most_recent_write_error_);
    most_recent_write_error_ = 0;
    most_recent_write_error_timestamp_ = QuicTime::Zero();
  }
}

bool QuicConnectionMigrationManager::MaybeCloseIdleSession(
    bool has_write_error, ConnectionCloseBehavior close_behavior) {
  if (session_->HasActiveRequestStreams()) {
    return false;
  }
  if (!config_.migrate_idle_session) {
    // Close the idle session.
    if (!has_write_error) {
      session_->OnConnectionToBeClosedDueToMigrationError(
          current_migration_cause_,
          QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS);
      connection_->CloseConnection(
          QUIC_CONNECTION_MIGRATION_NO_MIGRATABLE_STREAMS,
          "Migrating idle session is disabled.", close_behavior);
    } else {
      connection_->CloseConnection(QUIC_PACKET_WRITE_ERROR,
                                   "Write error for non-migratable session",
                                   close_behavior);
    }
    OnMigrationFailure(
        QuicConnectionMigrationStatus::MIGRATION_STATUS_NO_MIGRATABLE_STREAMS,
        "No active streams");
    return true;
  }
  // There are no active/drainning streams, check the last stream's finish time.
  if (session_->TimeSinceLastStreamClose() < config_.idle_migration_period) {
    // Still within the idle migration period.
    return false;
  }
  if (!has_write_error) {
    session_->OnConnectionToBeClosedDueToMigrationError(
        current_migration_cause_, QUIC_NETWORK_IDLE_TIMEOUT);
    connection_->CloseConnection(
        QUIC_NETWORK_IDLE_TIMEOUT,
        "Idle session exceeds configured idle migration period",
        ConnectionCloseBehavior::SILENT_CLOSE);
  } else {
    connection_->CloseConnection(QUIC_PACKET_WRITE_ERROR,
                                 "Write error for idle session",
                                 close_behavior);
  }
  OnMigrationFailure(
      QuicConnectionMigrationStatus::MIGRATION_STATUS_IDLE_MIGRATION_TIMEOUT,
      "Idle migration period exceeded");
  return true;
}

void QuicConnectionMigrationManager::OnHandshakeCompleted(
    const QuicConfig& negotiated_config) {
  migration_disabled_ = negotiated_config.DisableConnectionMigration();
  // Attempt to migrate back to the default network after handshake has been
  // completed if the session is not created on the default network.
  if (config_.migrate_session_on_network_change &&
      default_network_ != kInvalidNetworkHandle &&
      current_network() != default_network_) {
    QUICHE_DCHECK(session_->version().HasIetfQuicFrames());
    current_migration_cause_ =
        MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK;
    StartMigrateBackToDefaultNetworkTimer(
        QuicTimeDelta::FromSeconds(kMinRetryTimeForDefaultNetworkSecs));
  }
}

// TODO(fayang): Remove this when necessary data is collected.
void QuicConnectionMigrationManager::RecordProbeResultToHistogram(
    MigrationCause cause, bool success) {
  QUIC_CLIENT_HISTOGRAM_BOOL("QuicSession.PathValidationSuccess", success, "");
  switch (cause) {
    case MigrationCause::UNKNOWN_CAUSE:
      QUIC_CLIENT_HISTOGRAM_BOOL("QuicSession.PathValidationSuccess.Unknown",
                                 success, "");
      return;
    case MigrationCause::ON_NETWORK_CONNECTED:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.OnNetworkConnected", success, "");
      return;
    case MigrationCause::ON_NETWORK_DISCONNECTED:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.OnNetworkDisconnected", success,
          "");
      return;
    case MigrationCause::ON_WRITE_ERROR:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.OnWriteError", success, "");
      return;
    case MigrationCause::ON_NETWORK_MADE_DEFAULT:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.OnNetworkMadeDefault", success,
          "");
      return;
    case MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.OnMigrateBackToDefaultNetwork",
          success, "");
      return;
    case MigrationCause::CHANGE_NETWORK_ON_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.OnPathDegrading", success, "");
      return;
    case MigrationCause::NEW_NETWORK_CONNECTED_POST_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess."
          "NewNetworkConnectedPostPathDegrading",
          success, "");
      return;
    case MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess.PortMigration", success, "");
      return;
    case MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.PathValidationSuccess."
          "OnServerPreferredAddressAvailable",
          success, "");
      return;
  }
}

void QuicConnectionMigrationManager::ResetMigrationCauseAndLogResult(
    QuicConnectionMigrationStatus status) {
  if (current_migration_cause_ ==
      MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING) {
    QUIC_CLIENT_HISTOGRAM_ENUM(
        "QuicSession.PortMigration", status,
        QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
    current_migration_cause_ = MigrationCause::UNKNOWN_CAUSE;
    return;
  }
  if (current_migration_cause_ ==
      MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE) {
    QUIC_CLIENT_HISTOGRAM_ENUM(
        "QuicSession.OnServerPreferredAddressAvailable", status,
        QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
    current_migration_cause_ = MigrationCause::UNKNOWN_CAUSE;
    return;
  }
  QUIC_CLIENT_HISTOGRAM_ENUM(
      "QuicSession.ConnectionMigration", status,
      QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
  // Log the connection migraiton result to different histograms based on the
  // cause of the connection migration.
  switch (current_migration_cause_) {
    case MigrationCause::UNKNOWN_CAUSE:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.Unknown", status,
          QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::ON_NETWORK_CONNECTED:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.OnNetworkConnected", status,
          QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::ON_NETWORK_DISCONNECTED:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.OnNetworkDisconnected", status,
          QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::ON_WRITE_ERROR:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.OnWriteError", status,
          QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::ON_NETWORK_MADE_DEFAULT:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.OnNetworkMadeDefault", status,
          QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.OnMigrateBackToDefaultNetwork",
          status, QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::CHANGE_NETWORK_ON_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration.OnPathDegrading", status,
          QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::NEW_NETWORK_CONNECTED_POST_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_ENUM(
          "QuicSession.ConnectionMigration."
          "NewNetworkConnectedPostPathDegrading",
          status, QuicConnectionMigrationStatus::MIGRATION_STATUS_MAX, "");
      break;
    case MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING:
    case MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE:
      // Already has been handled above.
      break;
  }
  current_migration_cause_ = MigrationCause::UNKNOWN_CAUSE;
}

void QuicConnectionMigrationManager::RecordHandshakeStatusOnMigrationSignal()
    const {
  const bool handshake_confirmed = session_->OneRttKeysAvailable();
  if (current_migration_cause_ ==
      MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING) {
    QUIC_CLIENT_HISTOGRAM_BOOL("QuicSession.HandshakeStatusOnPortMigration",
                               handshake_confirmed, "");
    return;
  }
  if (current_migration_cause_ ==
      MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE) {
    QUIC_CLIENT_HISTOGRAM_BOOL(
        "QuicSession.HandshakeStatusOnMigratingToServerPreferredAddress",
        handshake_confirmed, "");
    return;
  }
  QUIC_CLIENT_HISTOGRAM_BOOL("QuicSession.HandshakeStatusOnConnectionMigration",
                             handshake_confirmed, "");
  switch (current_migration_cause_) {
    case MigrationCause::UNKNOWN_CAUSE:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration.Unknown",
          handshake_confirmed, "");
      break;
    case MigrationCause::ON_NETWORK_CONNECTED:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration."
          "OnNetworkConnected",
          handshake_confirmed, "");
      break;
    case MigrationCause::ON_NETWORK_DISCONNECTED:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration."
          "OnNetworkDisconnected",
          handshake_confirmed, "");
      break;
    case MigrationCause::ON_WRITE_ERROR:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration.OnWriteError",
          handshake_confirmed, "");
      break;
    case MigrationCause::ON_NETWORK_MADE_DEFAULT:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration."
          "OnNetworkMadeDefault",
          handshake_confirmed, "");
      break;
    case MigrationCause::ON_MIGRATE_BACK_TO_DEFAULT_NETWORK:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration."
          "OnMigrateBackToDefaultNetwork",
          handshake_confirmed, "");
      break;
    case MigrationCause::CHANGE_NETWORK_ON_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration."
          "OnPathDegrading",
          handshake_confirmed, "");
      break;
    case MigrationCause::NEW_NETWORK_CONNECTED_POST_PATH_DEGRADING:
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.HandshakeStatusOnConnectionMigration."
          "NewNetworkConnectedPostPathDegrading",
          handshake_confirmed, "");
      break;
    case MigrationCause::CHANGE_PORT_ON_PATH_DEGRADING:
    case MigrationCause::ON_SERVER_PREFERRED_ADDRESS_AVAILABLE:
      // Already has been handled above.
      break;
  }
}

void QuicConnectionMigrationManager::OnMigrationFailure(
    QuicConnectionMigrationStatus status, absl::string_view reason) {
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationFailed(
        current_migration_cause_, connection_->connection_id(), reason);
  }
  // |current_migration_cause_| will be reset afterwards.
  ResetMigrationCauseAndLogResult(status);
}

void QuicConnectionMigrationManager::OnMigrationSuccess() {
  if (debug_visitor_) {
    debug_visitor_->OnConnectionMigrationSuccess(current_migration_cause_,
                                                 connection_->connection_id());
  }
  // |current_migration_cause_| will be reset afterwards.
  ResetMigrationCauseAndLogResult(
      QuicConnectionMigrationStatus::MIGRATION_STATUS_SUCCESS);
}

}  // namespace quic
