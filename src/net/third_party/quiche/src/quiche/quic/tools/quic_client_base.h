// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A base class for the toy client, which connects to a specified port and sends
// QUIC request to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_CLIENT_BASE_H_
#define QUICHE_QUIC_TOOLS_QUIC_CLIENT_BASE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

class ProofVerifier;
class QuicServerId;
class SessionCache;

// A path context which owns the writer.
class QUIC_EXPORT_PRIVATE PathMigrationContext
    : public QuicPathValidationContext {
 public:
  PathMigrationContext(std::unique_ptr<QuicPacketWriter> writer,
                       const QuicSocketAddress& self_address,
                       const QuicSocketAddress& peer_address)
      : QuicPathValidationContext(self_address, peer_address),
        alternative_writer_(std::move(writer)) {}

  QuicPacketWriter* WriterToUse() override { return alternative_writer_.get(); }

  QuicPacketWriter* ReleaseWriter() { return alternative_writer_.release(); }

 private:
  std::unique_ptr<QuicPacketWriter> alternative_writer_;
};

// QuicClientBase handles establishing a connection to the passed in
// server id, including ensuring that it supports the passed in versions
// and config.
// Subclasses derived from this class are responsible for creating the
// actual QuicSession instance, as well as defining functions that
// create and run the underlying network transport.
class QuicClientBase : public QuicSession::Visitor {
 public:
  // An interface to various network events that the QuicClient will need to
  // interact with.
  class NetworkHelper {
   public:
    virtual ~NetworkHelper();

    // Runs one iteration of the event loop.
    virtual void RunEventLoop() = 0;

    // Used during initialization: creates the UDP socket FD, sets socket
    // options, and binds the socket to our address.
    virtual bool CreateUDPSocketAndBind(QuicSocketAddress server_address,
                                        QuicIpAddress bind_to_address,
                                        int bind_to_port) = 0;

    // Unregister and close all open UDP sockets.
    virtual void CleanUpAllUDPSockets() = 0;

    // If the client has at least one UDP socket, return address of the latest
    // created one. Otherwise, return an empty socket address.
    virtual QuicSocketAddress GetLatestClientAddress() const = 0;

    // Creates a packet writer to be used for the next connection.
    virtual QuicPacketWriter* CreateQuicPacketWriter() = 0;
  };

  QuicClientBase(const QuicServerId& server_id,
                 const ParsedQuicVersionVector& supported_versions,
                 const QuicConfig& config,
                 QuicConnectionHelperInterface* helper,
                 QuicAlarmFactory* alarm_factory,
                 std::unique_ptr<NetworkHelper> network_helper,
                 std::unique_ptr<ProofVerifier> proof_verifier,
                 std::unique_ptr<SessionCache> session_cache);
  QuicClientBase(const QuicClientBase&) = delete;
  QuicClientBase& operator=(const QuicClientBase&) = delete;

  virtual ~QuicClientBase();

  // Implmenets QuicSession::Visitor
  void OnConnectionClosed(QuicConnectionId /*server_connection_id*/,
                          QuicErrorCode /*error*/,
                          const std::string& /*error_details*/,
                          ConnectionCloseSource /*source*/) override {}

  void OnWriteBlocked(QuicBlockedWriterInterface* /*blocked_writer*/) override {
  }
  void OnRstStreamReceived(const QuicRstStreamFrame& /*frame*/) override {}
  void OnStopSendingReceived(const QuicStopSendingFrame& /*frame*/) override {}
  bool TryAddNewConnectionId(
      const QuicConnectionId& /*server_connection_id*/,
      const QuicConnectionId& /*new_connection_id*/) override {
    return false;
  }
  void OnConnectionIdRetired(
      const QuicConnectionId& /*server_connection_id*/) override {}
  void OnServerPreferredAddressAvailable(
      const QuicSocketAddress& server_preferred_address) override;
  void OnPathDegrading() override;

  // Initializes the client to create a connection. Should be called exactly
  // once before calling StartConnect or Connect. Returns true if the
  // initialization succeeds, false otherwise.
  virtual bool Initialize();

  // "Connect" to the QUIC server, including performing synchronous crypto
  // handshake.
  bool Connect();

  // Start the crypto handshake.  This can be done in place of the synchronous
  // Connect(), but callers are responsible for making sure the crypto handshake
  // completes.
  void StartConnect();

  // Calls session()->Initialize(). Subclasses may override this if any extra
  // initialization needs to be done. Subclasses should expect that session()
  // is non-null and valid.
  virtual void InitializeSession();

  // Disconnects from the QUIC server.
  void Disconnect();

  // Returns true if the crypto handshake has yet to establish encryption.
  // Returns false if encryption is active (even if the server hasn't confirmed
  // the handshake) or if the connection has been closed.
  bool EncryptionBeingEstablished();

  // Wait for events until the stream with the given ID is closed.
  void WaitForStreamToClose(QuicStreamId id);

  // Wait for 1-RTT keys become available.
  // Returns true once 1-RTT keys are available, false otherwise.
  ABSL_MUST_USE_RESULT bool WaitForOneRttKeysAvailable();

  // Wait for handshake state proceeds to HANDSHAKE_CONFIRMED.
  // In QUIC crypto, this does the same as WaitForOneRttKeysAvailable, while in
  // TLS, this waits for HANDSHAKE_DONE frame is received.
  ABSL_MUST_USE_RESULT bool WaitForHandshakeConfirmed();

  // Wait up to 50ms, and handle any events which occur.
  // Returns true if there are any outstanding requests.
  bool WaitForEvents();

  // Performs the part of WaitForEvents() that is done after the actual event
  // loop call.
  bool WaitForEventsPostprocessing();

  // Migrate to a new socket (new_host) during an active connection.
  bool MigrateSocket(const QuicIpAddress& new_host);

  // Migrate to a new socket (new_host, port) during an active connection.
  bool MigrateSocketWithSpecifiedPort(const QuicIpAddress& new_host, int port);

  // Validate the new socket and migrate to it if the validation succeeds.
  // Otherwise stay on the current socket. Return true if the validation has
  // started.
  bool ValidateAndMigrateSocket(const QuicIpAddress& new_host);

  // Open a new socket to change to a new ephemeral port.
  bool ChangeEphemeralPort();

  QuicSession* session();
  const QuicSession* session() const;

  bool connected() const;
  virtual bool goaway_received() const;

  const QuicServerId& server_id() const { return server_id_; }

  // This should only be set before the initial Connect()
  void set_server_id(const QuicServerId& server_id) { server_id_ = server_id; }

  void SetUserAgentID(const std::string& user_agent_id) {
    crypto_config_.set_user_agent_id(user_agent_id);
  }

  void SetPreferredGroups(const std::vector<uint16_t>& preferred_groups) {
    crypto_config_.set_preferred_groups(preferred_groups);
  }

  void SetTlsSignatureAlgorithms(std::string signature_algorithms) {
    crypto_config_.set_tls_signature_algorithms(
        std::move(signature_algorithms));
  }

  const ParsedQuicVersionVector& supported_versions() const {
    return supported_versions_;
  }

  void SetSupportedVersions(const ParsedQuicVersionVector& versions) {
    supported_versions_ = versions;
  }

  QuicConfig* config() { return &config_; }

  QuicCryptoClientConfig* crypto_config() { return &crypto_config_; }

  // Change the initial maximum packet size of the connection.  Has to be called
  // before Connect()/StartConnect() in order to have any effect.
  void set_initial_max_packet_length(QuicByteCount initial_max_packet_length) {
    initial_max_packet_length_ = initial_max_packet_length;
  }

  // The number of client hellos sent.
  int GetNumSentClientHellos();

  // Returns true if early data (0-RTT data) was sent and the server accepted
  // it.
  virtual bool EarlyDataAccepted() = 0;

  // Returns true if the handshake was delayed one round trip by the server
  // because the server wanted proof the client controls its source address
  // before progressing further. In Google QUIC, this would be due to an
  // inchoate REJ in the QUIC Crypto handshake; in IETF QUIC this would be due
  // to a Retry packet.
  // TODO(nharper): Consider a better name for this method.
  virtual bool ReceivedInchoateReject() = 0;

  // Gather the stats for the last session and update the stats for the overall
  // connection.
  void UpdateStats();

  // The number of server config updates received.
  int GetNumReceivedServerConfigUpdates();

  // Returns any errors that occurred at the connection-level.
  QuicErrorCode connection_error() const;
  void set_connection_error(QuicErrorCode connection_error) {
    connection_error_ = connection_error;
  }

  bool connected_or_attempting_connect() const {
    return connected_or_attempting_connect_;
  }
  void set_connected_or_attempting_connect(
      bool connected_or_attempting_connect) {
    connected_or_attempting_connect_ = connected_or_attempting_connect;
  }

  QuicPacketWriter* writer() { return writer_.get(); }
  void set_writer(QuicPacketWriter* writer) {
    if (writer_.get() != writer) {
      writer_.reset(writer);
    }
  }

  void reset_writer() { writer_.reset(); }

  ProofVerifier* proof_verifier() const;

  void set_bind_to_address(QuicIpAddress address) {
    bind_to_address_ = address;
  }

  QuicIpAddress bind_to_address() const { return bind_to_address_; }

  void set_local_port(int local_port) { local_port_ = local_port; }

  int local_port() const { return local_port_; }

  const QuicSocketAddress& server_address() const { return server_address_; }

  void set_server_address(const QuicSocketAddress& server_address) {
    server_address_ = server_address;
  }

  QuicConnectionHelperInterface* helper() { return helper_.get(); }

  QuicAlarmFactory* alarm_factory() { return alarm_factory_.get(); }

  NetworkHelper* network_helper();
  const NetworkHelper* network_helper() const;

  bool initialized() const { return initialized_; }

  void SetPreSharedKey(absl::string_view key) {
    crypto_config_.set_pre_shared_key(key);
  }

  void set_connection_debug_visitor(
      QuicConnectionDebugVisitor* connection_debug_visitor) {
    connection_debug_visitor_ = connection_debug_visitor;
  }

  // Sets the interface name to bind. If empty, will not attempt to bind the
  // socket to that interface. Defaults to empty string.
  void set_interface_name(std::string interface_name) {
    interface_name_ = interface_name;
  }

  std::string interface_name() const { return interface_name_; }

  void set_server_connection_id_override(
      const QuicConnectionId& connection_id) {
    server_connection_id_override_ = connection_id;
  }

  void set_server_connection_id_length(uint8_t server_connection_id_length) {
    server_connection_id_length_ = server_connection_id_length;
  }

  void set_client_connection_id_length(uint8_t client_connection_id_length) {
    client_connection_id_length_ = client_connection_id_length;
  }

  bool HasPendingPathValidation();

  void ValidateNewNetwork(const QuicIpAddress& host);

  void AddValidatedPath(std::unique_ptr<QuicPathValidationContext> context) {
    validated_paths_.push_back(std::move(context));
  }

  const std::vector<std::unique_ptr<QuicPathValidationContext>>&
  validated_paths() const {
    return validated_paths_;
  }

  // Enable port migration upon path degrading after given number of PTOs.
  // If no value is provided, path degrading will be detected after 4 PTOs by
  // default.
  void EnablePortMigrationUponPathDegrading(
      std::optional<int> num_ptos_for_path_degrading) {
    allow_port_migration_ = true;
    if (num_ptos_for_path_degrading.has_value()) {
      session_->connection()
          ->sent_packet_manager()
          .set_num_ptos_for_path_degrading(num_ptos_for_path_degrading.value());
    }
  }

  virtual void OnSocketMigrationProbingSuccess(
      std::unique_ptr<QuicPathValidationContext> context);

  virtual void OnSocketMigrationProbingFailure() {}

 protected:
  // TODO(rch): Move GetNumSentClientHellosFromSession and
  // GetNumReceivedServerConfigUpdatesFromSession into a new/better
  // QuicSpdyClientSession class. The current inherits dependencies from
  // Spdy. When that happens this class and all its subclasses should
  // work with QuicSpdyClientSession instead of QuicSession.
  // That will obviate the need for the pure virtual functions below.

  // Extract the number of sent client hellos from the session.
  virtual int GetNumSentClientHellosFromSession() = 0;

  // The number of server config updates received.
  virtual int GetNumReceivedServerConfigUpdatesFromSession() = 0;

  // If this client supports buffering data, resend it.
  virtual void ResendSavedData() = 0;

  // If this client supports buffering data, clear it.
  virtual void ClearDataToResend() = 0;

  // Takes ownership of |connection|. If you override this function,
  // you probably want to call ResetSession() in your destructor.
  // TODO(rch): Change the connection parameter to take in a
  // std::unique_ptr<QuicConnection> instead.
  virtual std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) = 0;

  // Generates the next ConnectionId for |server_id_|.  By default, if the
  // cached server config contains a server-designated ID, that ID will be
  // returned.  Otherwise, the next random ID will be returned.
  QuicConnectionId GetNextConnectionId();

  // Generates a new, random connection ID (as opposed to a server-designated
  // connection ID).
  virtual QuicConnectionId GenerateNewConnectionId();

  // Returns the client connection ID to use.
  virtual QuicConnectionId GetClientConnectionId();

  // Subclasses may need to explicitly clear the session on destruction
  // if they create it with objects that will be destroyed before this is.
  // You probably want to call this if you override CreateQuicSpdyClientSession.
  void ResetSession() { session_.reset(); }

  // Returns true if the corresponding of this client has active requests.
  virtual bool HasActiveRequests() = 0;

  // Allows derived classes to access this when creating connections.
  ConnectionIdGeneratorInterface& connection_id_generator();

 private:
  // Returns true and set |version| if client can reconnect with a different
  // version.
  bool CanReconnectWithDifferentVersion(ParsedQuicVersion* version) const;

  std::unique_ptr<QuicPacketWriter> CreateWriterForNewNetwork(
      const QuicIpAddress& new_host, int port);

  // |server_id_| is a tuple (hostname, port, is_https) of the server.
  QuicServerId server_id_;

  // Tracks if the client is initialized to connect.
  bool initialized_;

  // Address of the server.
  QuicSocketAddress server_address_;

  // If initialized, the address to bind to.
  QuicIpAddress bind_to_address_;

  // Local port to bind to. Initialize to 0.
  int local_port_;

  // config_ and crypto_config_ contain configuration and cached state about
  // servers.
  QuicConfig config_;
  QuicCryptoClientConfig crypto_config_;

  // Helper to be used by created connections. Must outlive |session_|.
  std::unique_ptr<QuicConnectionHelperInterface> helper_;

  // Alarm factory to be used by created connections. Must outlive |session_|.
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;

  // Writer used to actually send packets to the wire. Must outlive |session_|.
  std::unique_ptr<QuicPacketWriter> writer_;

  // Session which manages streams.
  std::unique_ptr<QuicSession> session_;

  // This vector contains QUIC versions which we currently support.
  // This should be ordered such that the highest supported version is the first
  // element, with subsequent elements in descending order (versions can be
  // skipped as necessary). We will always pick supported_versions_[0] as the
  // initial version to use.
  ParsedQuicVersionVector supported_versions_;

  // The initial value of maximum packet size of the connection.  If set to
  // zero, the default is used.
  QuicByteCount initial_max_packet_length_;

  // The number of hellos sent during the current/latest connection.
  int num_sent_client_hellos_;

  // Used to store any errors that occurred with the overall connection (as
  // opposed to that associated with the last session object).
  QuicErrorCode connection_error_;

  // True when the client is attempting to connect.  Set to false between a call
  // to Disconnect() and the subsequent call to StartConnect().  When
  // connected_or_attempting_connect_ is false, the session object corresponds
  // to the previous client-level connection.
  bool connected_or_attempting_connect_;

  // The network helper used to create sockets and manage the event loop.
  // Not owned by this class.
  std::unique_ptr<NetworkHelper> network_helper_;

  // The debug visitor set on the connection right after it is constructed.
  // Not owned, must be valid for the lifetime of the QuicClientBase instance.
  QuicConnectionDebugVisitor* connection_debug_visitor_;

  // If set,
  // - GetNextConnectionId will use this as the next server connection id.
  // - GenerateNewConnectionId will not be called.
  std::optional<QuicConnectionId> server_connection_id_override_;

  // GenerateNewConnectionId creates a random connection ID of this length.
  // Defaults to 8.
  uint8_t server_connection_id_length_;

  // GetClientConnectionId creates a random connection ID of this length.
  // Defaults to 0.
  uint8_t client_connection_id_length_;

  // Stores validated paths.
  std::vector<std::unique_ptr<QuicPathValidationContext>> validated_paths_;

  // Stores the interface name to bind. If empty, will not attempt to bind the
  // socket to that interface. Defaults to empty string.
  std::string interface_name_;

  DeterministicConnectionIdGenerator connection_id_generator_{
      kQuicDefaultConnectionIdLength};

  bool allow_port_migration_{false};
  uint32_t num_path_degrading_handled_{0};
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_CLIENT_BASE_H_
