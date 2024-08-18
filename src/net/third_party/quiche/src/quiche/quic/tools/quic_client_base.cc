// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_client_base.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_crypto_client_stream.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {

// Implements the basic feature of a result delegate for path validation for
// connection migration. If the validation succeeds, migrate to the alternative
// path. Otherwise, stay on the current path.
class QuicClientSocketMigrationValidationResultDelegate
    : public QuicPathValidator::ResultDelegate {
 public:
  explicit QuicClientSocketMigrationValidationResultDelegate(
      QuicClientBase* client)
      : QuicPathValidator::ResultDelegate(), client_(client) {}

  virtual ~QuicClientSocketMigrationValidationResultDelegate() = default;

  // QuicPathValidator::ResultDelegate
  // Overridden to start migration and takes the ownership of the writer in the
  // context.
  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context,
      QuicTime /*start_time*/) override {
    QUIC_DLOG(INFO) << "Successfully validated path from " << *context
                    << ". Migrate to it now.";
    client_->OnSocketMigrationProbingSuccess(std::move(context));
  }

  void OnPathValidationFailure(
      std::unique_ptr<QuicPathValidationContext> context) override {
    QUIC_LOG(WARNING) << "Fail to validate path " << *context
                      << ", stop migrating.";
    client_->OnSocketMigrationProbingFailure();
    client_->session()->connection()->OnPathValidationFailureAtClient(
        /*is_multi_port=*/false, *context);
  }

 protected:
  QuicClientBase* client() { return client_; }

 private:
  QuicClientBase* client_;
};

class ServerPreferredAddressResultDelegateWithWriter
    : public QuicClientSocketMigrationValidationResultDelegate {
 public:
  ServerPreferredAddressResultDelegateWithWriter(QuicClientBase* client)
      : QuicClientSocketMigrationValidationResultDelegate(client) {}

  // Overridden to transfer the ownership of the new writer.
  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context,
      QuicTime /*start_time*/) override {
    client()->session()->connection()->OnServerPreferredAddressValidated(
        *context, false);
    auto migration_context = std::unique_ptr<PathMigrationContext>(
        static_cast<PathMigrationContext*>(context.release()));
    client()->set_writer(migration_context->ReleaseWriter());
  }
};

}  // namespace

void QuicClientBase::OnSocketMigrationProbingSuccess(
    std::unique_ptr<QuicPathValidationContext> context) {
  auto migration_context = std::unique_ptr<PathMigrationContext>(
      static_cast<PathMigrationContext*>(context.release()));
  session()->MigratePath(
      migration_context->self_address(), migration_context->peer_address(),
      migration_context->WriterToUse(), /*owns_writer=*/false);
  QUICHE_DCHECK(migration_context->WriterToUse() != nullptr);
  // Hand the ownership of the alternative writer to the client.
  set_writer(migration_context->ReleaseWriter());
}

QuicClientBase::NetworkHelper::~NetworkHelper() = default;

QuicClientBase::QuicClientBase(
    const QuicServerId& server_id,
    const ParsedQuicVersionVector& supported_versions, const QuicConfig& config,
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    std::unique_ptr<NetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier,
    std::unique_ptr<SessionCache> session_cache)
    : server_id_(server_id),
      initialized_(false),
      local_port_(0),
      config_(config),
      crypto_config_(std::move(proof_verifier), std::move(session_cache)),
      helper_(helper),
      alarm_factory_(alarm_factory),
      supported_versions_(supported_versions),
      initial_max_packet_length_(0),
      num_sent_client_hellos_(0),
      connection_error_(QUIC_NO_ERROR),
      connected_or_attempting_connect_(false),
      network_helper_(std::move(network_helper)),
      connection_debug_visitor_(nullptr),
      server_connection_id_length_(kQuicDefaultConnectionIdLength),
      client_connection_id_length_(0) {}

QuicClientBase::~QuicClientBase() = default;

bool QuicClientBase::Initialize() {
  num_sent_client_hellos_ = 0;
  connection_error_ = QUIC_NO_ERROR;
  connected_or_attempting_connect_ = false;

  // If an initial flow control window has not explicitly been set, then use the
  // same values that Chrome uses.
  const uint32_t kSessionMaxRecvWindowSize = 15 * 1024 * 1024;  // 15 MB
  const uint32_t kStreamMaxRecvWindowSize = 6 * 1024 * 1024;    //  6 MB
  if (config()->GetInitialStreamFlowControlWindowToSend() ==
      kDefaultFlowControlSendWindow) {
    config()->SetInitialStreamFlowControlWindowToSend(kStreamMaxRecvWindowSize);
  }
  if (config()->GetInitialSessionFlowControlWindowToSend() ==
      kDefaultFlowControlSendWindow) {
    config()->SetInitialSessionFlowControlWindowToSend(
        kSessionMaxRecvWindowSize);
  }

  if (!network_helper_->CreateUDPSocketAndBind(server_address_,
                                               bind_to_address_, local_port_)) {
    return false;
  }

  initialized_ = true;
  return true;
}

bool QuicClientBase::Connect() {
  // Attempt multiple connects until the maximum number of client hellos have
  // been sent.
  int num_attempts = 0;
  while (!connected() &&
         num_attempts <= QuicCryptoClientStream::kMaxClientHellos) {
    StartConnect();
    while (EncryptionBeingEstablished()) {
      WaitForEvents();
    }
    ParsedQuicVersion version = UnsupportedQuicVersion();
    if (session() != nullptr && !CanReconnectWithDifferentVersion(&version)) {
      // We've successfully created a session but we're not connected, and we
      // cannot reconnect with a different version.  Give up trying.
      break;
    }
    num_attempts++;
  }
  if (session() == nullptr) {
    QUIC_BUG(quic_bug_10906_1) << "Missing session after Connect";
    return false;
  }
  return session()->connection()->connected();
}

void QuicClientBase::StartConnect() {
  QUICHE_DCHECK(initialized_);
  QUICHE_DCHECK(!connected());
  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  ParsedQuicVersion mutual_version = UnsupportedQuicVersion();
  const bool can_reconnect_with_different_version =
      CanReconnectWithDifferentVersion(&mutual_version);
  if (connected_or_attempting_connect()) {
    // Clear queued up data if client can not try to connect with a different
    // version.
    if (!can_reconnect_with_different_version) {
      ClearDataToResend();
    }
    // Before we destroy the last session and create a new one, gather its stats
    // and update the stats for the overall connection.
    UpdateStats();
  }

  const quic::ParsedQuicVersionVector client_supported_versions =
      can_reconnect_with_different_version
          ? ParsedQuicVersionVector{mutual_version}
          : supported_versions();

  session_ = CreateQuicClientSession(
      client_supported_versions,
      new QuicConnection(GetNextConnectionId(), QuicSocketAddress(),
                         server_address(), helper(), alarm_factory(), writer,
                         /* owns_writer= */ false, Perspective::IS_CLIENT,
                         client_supported_versions, connection_id_generator_));
  if (can_reconnect_with_different_version) {
    session()->set_client_original_supported_versions(supported_versions());
  }
  if (connection_debug_visitor_ != nullptr) {
    session()->connection()->set_debug_visitor(connection_debug_visitor_);
  }
  session()->connection()->set_client_connection_id(GetClientConnectionId());
  if (initial_max_packet_length_ != 0) {
    session()->connection()->SetMaxPacketLength(initial_max_packet_length_);
  }
  // Reset |writer()| after |session()| so that the old writer outlives the old
  // session.
  set_writer(writer);
  InitializeSession();
  if (can_reconnect_with_different_version) {
    // This is a reconnect using server supported |mutual_version|.
    session()->connection()->SetVersionNegotiated();
  }
  set_connected_or_attempting_connect(true);
  num_path_degrading_handled_ = 0;
}

void QuicClientBase::InitializeSession() { session()->Initialize(); }

void QuicClientBase::Disconnect() {
  QUICHE_DCHECK(initialized_);

  initialized_ = false;
  if (connected()) {
    session()->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Client disconnecting",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  ClearDataToResend();

  network_helper_->CleanUpAllUDPSockets();
}

ProofVerifier* QuicClientBase::proof_verifier() const {
  return crypto_config_.proof_verifier();
}

bool QuicClientBase::EncryptionBeingEstablished() {
  return !session_->IsEncryptionEstablished() &&
         session_->connection()->connected();
}

bool QuicClientBase::WaitForEvents() {
  if (!connected()) {
    QUIC_BUG(quic_bug_10906_2)
        << "Cannot call WaitForEvents on non-connected client";
    return false;
  }

  network_helper_->RunEventLoop();

  return WaitForEventsPostprocessing();
}

bool QuicClientBase::WaitForEventsPostprocessing() {
  QUICHE_DCHECK(session() != nullptr);
  ParsedQuicVersion version = UnsupportedQuicVersion();
  if (!connected() && CanReconnectWithDifferentVersion(&version)) {
    QUIC_DLOG(INFO) << "Can reconnect with version: " << version
                    << ", attempting to reconnect.";

    Connect();
  }

  return HasActiveRequests();
}

bool QuicClientBase::MigrateSocket(const QuicIpAddress& new_host) {
  return MigrateSocketWithSpecifiedPort(new_host, local_port_);
}

bool QuicClientBase::MigrateSocketWithSpecifiedPort(
    const QuicIpAddress& new_host, int port) {
  if (!connected()) {
    QUICHE_DVLOG(1)
        << "MigrateSocketWithSpecifiedPort failed as connection has closed";
    return false;
  }

  network_helper_->CleanUpAllUDPSockets();
  std::unique_ptr<QuicPacketWriter> writer =
      CreateWriterForNewNetwork(new_host, port);
  if (writer == nullptr) {
    QUICHE_DVLOG(1)
        << "MigrateSocketWithSpecifiedPort failed from writer creation";
    return false;
  }
  if (!session()->MigratePath(network_helper_->GetLatestClientAddress(),
                              session()->connection()->peer_address(),
                              writer.get(), false)) {
    QUICHE_DVLOG(1)
        << "MigrateSocketWithSpecifiedPort failed from session()->MigratePath";
    return false;
  }
  set_writer(writer.release());
  return true;
}

bool QuicClientBase::ValidateAndMigrateSocket(const QuicIpAddress& new_host) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(
      session_->connection()->version().transport_version));
  if (!connected()) {
    return false;
  }

  std::unique_ptr<QuicPacketWriter> writer =
      CreateWriterForNewNetwork(new_host, local_port_);
  if (writer == nullptr) {
    return false;
  }
  // Asynchronously start migration.
  session_->ValidatePath(
      std::make_unique<PathMigrationContext>(
          std::move(writer), network_helper_->GetLatestClientAddress(),
          session_->peer_address()),
      std::make_unique<QuicClientSocketMigrationValidationResultDelegate>(this),
      PathValidationReason::kConnectionMigration);
  return true;
}

std::unique_ptr<QuicPacketWriter> QuicClientBase::CreateWriterForNewNetwork(
    const QuicIpAddress& new_host, int port) {
  set_bind_to_address(new_host);
  set_local_port(port);
  if (!network_helper_->CreateUDPSocketAndBind(server_address_,
                                               bind_to_address_, port)) {
    return nullptr;
  }

  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  QUIC_LOG_IF(WARNING, writer == writer_.get())
      << "The new writer is wrapped in the same wrapper as the old one, thus "
         "appearing to have the same address as the old one.";
  return std::unique_ptr<QuicPacketWriter>(writer);
}

bool QuicClientBase::ChangeEphemeralPort() {
  auto current_host = network_helper_->GetLatestClientAddress().host();
  return MigrateSocketWithSpecifiedPort(current_host, 0 /*any ephemeral port*/);
}

QuicSession* QuicClientBase::session() { return session_.get(); }

const QuicSession* QuicClientBase::session() const { return session_.get(); }

QuicClientBase::NetworkHelper* QuicClientBase::network_helper() {
  return network_helper_.get();
}

const QuicClientBase::NetworkHelper* QuicClientBase::network_helper() const {
  return network_helper_.get();
}

void QuicClientBase::WaitForStreamToClose(QuicStreamId id) {
  if (!connected()) {
    QUIC_BUG(quic_bug_10906_3)
        << "Cannot WaitForStreamToClose on non-connected client";
    return;
  }

  while (connected() && !session_->IsClosedStream(id)) {
    WaitForEvents();
  }
}

bool QuicClientBase::WaitForOneRttKeysAvailable() {
  if (!connected()) {
    QUIC_BUG(quic_bug_10906_4)
        << "Cannot WaitForOneRttKeysAvailable on non-connected client";
    return false;
  }

  while (connected() && !session_->OneRttKeysAvailable()) {
    WaitForEvents();
  }

  // If the handshake fails due to a timeout, the connection will be closed.
  QUIC_LOG_IF(ERROR, !connected()) << "Handshake with server failed.";
  return connected();
}

bool QuicClientBase::WaitForHandshakeConfirmed() {
  if (!session_->connection()->version().UsesTls()) {
    return WaitForOneRttKeysAvailable();
  }
  // Otherwise, wait for receipt of HANDSHAKE_DONE frame.
  while (connected() && session_->GetHandshakeState() < HANDSHAKE_CONFIRMED) {
    WaitForEvents();
  }

  // If the handshake fails due to a timeout, the connection will be closed.
  QUIC_LOG_IF(ERROR, !connected()) << "Handshake with server failed.";
  return connected();
}

bool QuicClientBase::connected() const {
  return session_.get() && session_->connection() &&
         session_->connection()->connected();
}

bool QuicClientBase::goaway_received() const {
  return session_ != nullptr && session_->transport_goaway_received();
}

int QuicClientBase::GetNumSentClientHellos() {
  // If we are not actively attempting to connect, the session object
  // corresponds to the previous connection and should not be used.
  const int current_session_hellos = !connected_or_attempting_connect_
                                         ? 0
                                         : GetNumSentClientHellosFromSession();
  return num_sent_client_hellos_ + current_session_hellos;
}

void QuicClientBase::UpdateStats() {
  num_sent_client_hellos_ += GetNumSentClientHellosFromSession();
}

int QuicClientBase::GetNumReceivedServerConfigUpdates() {
  // If we are not actively attempting to connect, the session object
  // corresponds to the previous connection and should not be used.
  return !connected_or_attempting_connect_
             ? 0
             : GetNumReceivedServerConfigUpdatesFromSession();
}

QuicErrorCode QuicClientBase::connection_error() const {
  // Return the high-level error if there was one.  Otherwise, return the
  // connection error from the last session.
  if (connection_error_ != QUIC_NO_ERROR) {
    return connection_error_;
  }
  if (session_ == nullptr) {
    return QUIC_NO_ERROR;
  }
  return session_->error();
}

QuicConnectionId QuicClientBase::GetNextConnectionId() {
  if (server_connection_id_override_.has_value()) {
    return *server_connection_id_override_;
  }
  return GenerateNewConnectionId();
}

QuicConnectionId QuicClientBase::GenerateNewConnectionId() {
  return QuicUtils::CreateRandomConnectionId(server_connection_id_length_);
}

QuicConnectionId QuicClientBase::GetClientConnectionId() {
  return QuicUtils::CreateRandomConnectionId(client_connection_id_length_);
}

bool QuicClientBase::CanReconnectWithDifferentVersion(
    ParsedQuicVersion* version) const {
  if (session_ == nullptr || session_->connection() == nullptr ||
      session_->error() != QUIC_INVALID_VERSION) {
    return false;
  }

  const auto& server_supported_versions =
      session_->connection()->server_supported_versions();
  if (server_supported_versions.empty()) {
    return false;
  }

  for (const auto& client_version : supported_versions_) {
    if (std::find(server_supported_versions.begin(),
                  server_supported_versions.end(),
                  client_version) != server_supported_versions.end()) {
      *version = client_version;
      return true;
    }
  }
  return false;
}

bool QuicClientBase::HasPendingPathValidation() {
  return session()->HasPendingPathValidation();
}

class ValidationResultDelegate : public QuicPathValidator::ResultDelegate {
 public:
  ValidationResultDelegate(QuicClientBase* client)
      : QuicPathValidator::ResultDelegate(), client_(client) {}

  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context,
      QuicTime start_time) override {
    QUIC_DLOG(INFO) << "Successfully validated path from " << *context
                    << ", validation started at " << start_time;
    client_->AddValidatedPath(std::move(context));
  }
  void OnPathValidationFailure(
      std::unique_ptr<QuicPathValidationContext> context) override {
    QUIC_LOG(WARNING) << "Fail to validate path " << *context
                      << ", stop migrating.";
    client_->session()->connection()->OnPathValidationFailureAtClient(
        /*is_multi_port=*/false, *context);
  }

 private:
  QuicClientBase* client_;
};

void QuicClientBase::ValidateNewNetwork(const QuicIpAddress& host) {
  std::unique_ptr<QuicPacketWriter> writer =
      CreateWriterForNewNetwork(host, local_port_);
  auto result_delegate = std::make_unique<ValidationResultDelegate>(this);
  if (writer == nullptr) {
    result_delegate->OnPathValidationFailure(
        std::make_unique<PathMigrationContext>(
            nullptr, network_helper_->GetLatestClientAddress(),
            session_->peer_address()));
    return;
  }
  session()->ValidatePath(
      std::make_unique<PathMigrationContext>(
          std::move(writer), network_helper_->GetLatestClientAddress(),
          session_->peer_address()),
      std::move(result_delegate), PathValidationReason::kConnectionMigration);
}

void QuicClientBase::OnServerPreferredAddressAvailable(
    const QuicSocketAddress& server_preferred_address) {
  const auto self_address = session_->self_address();
  if (network_helper_ == nullptr ||
      !network_helper_->CreateUDPSocketAndBind(server_preferred_address,
                                               self_address.host(), 0)) {
    return;
  }
  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  if (writer == nullptr) {
    return;
  }
  session()->ValidatePath(
      std::make_unique<PathMigrationContext>(
          std::unique_ptr<QuicPacketWriter>(writer),
          network_helper_->GetLatestClientAddress(), server_preferred_address),
      std::make_unique<ServerPreferredAddressResultDelegateWithWriter>(this),
      PathValidationReason::kServerPreferredAddressMigration);
}

void QuicClientBase::OnPathDegrading() {
  if (!allow_port_migration_ ||
      session_->GetHandshakeState() != HANDSHAKE_CONFIRMED ||
      session_->HasPendingPathValidation() ||
      session_->connection()->multi_port_stats() != nullptr ||
      config_.DisableConnectionMigration()) {
    return;
  }
  if (num_path_degrading_handled_ >=
      GetQuicFlag(quic_max_num_path_degrading_to_mitigate)) {
    QUIC_CODE_COUNT(reached_port_migration_upper_limit);
    return;
  }
  const auto self_address = session_->self_address();
  if (network_helper_ == nullptr ||
      !network_helper_->CreateUDPSocketAndBind(session_->peer_address(),
                                               self_address.host(), 0)) {
    return;
  }
  QuicPacketWriter* writer = network_helper_->CreateQuicPacketWriter();
  if (writer == nullptr) {
    return;
  }
  ++num_path_degrading_handled_;
  session()->ValidatePath(
      std::make_unique<PathMigrationContext>(
          std::unique_ptr<QuicPacketWriter>(writer),
          network_helper_->GetLatestClientAddress(), session_->peer_address()),
      std::make_unique<QuicClientSocketMigrationValidationResultDelegate>(this),
      PathValidationReason::kPortMigration);
  if (!session()->HasPendingPathValidation()) {
    QUIC_CODE_COUNT(fail_to_probe_new_path_after_current_one_degraded);
  }
}

}  // namespace quic
