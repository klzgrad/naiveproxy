// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A QuicSession, which demuxes a single connection to individual streams.

#ifndef QUICHE_QUIC_CORE_QUIC_SESSION_H_
#define QUICHE_QUIC_CORE_QUIC_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/tls_connection.h"
#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"
#include "quiche/quic/core/frames/quic_stop_sending_frame.h"
#include "quiche/quic/core/frames/quic_window_update_frame.h"
#include "quiche/quic/core/handshaker_delegate_interface.h"
#include "quiche/quic/core/legacy_quic_stream_id_manager.h"
#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_control_frame_manager.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_datagram_queue.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_stream_frame_data_producer.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_write_blocked_list.h"
#include "quiche/quic/core/session_notifier_interface.h"
#include "quiche/quic/core/stream_delegate_interface.h"
#include "quiche/quic/core/uber_quic_stream_id_manager.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

class QuicCryptoStream;
class QuicFlowController;
class QuicStream;
class QuicStreamIdManager;

namespace test {
class QuicSessionPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicSession
    : public QuicConnectionVisitorInterface,
      public SessionNotifierInterface,
      public QuicStreamFrameDataProducer,
      public QuicStreamIdManager::DelegateInterface,
      public HandshakerDelegateInterface,
      public StreamDelegateInterface,
      public QuicControlFrameManager::DelegateInterface {
 public:
  // An interface from the session to the entity owning the session.
  // This lets the session notify its owner when the connection
  // is closed, blocked, etc.
  // TODO(danzh): split this visitor to separate visitors for client and server
  // respectively as not all methods in this class are interesting to both
  // perspectives.
  class QUIC_EXPORT_PRIVATE Visitor {
   public:
    virtual ~Visitor() {}

    // Called when the connection is closed after the streams have been closed.
    virtual void OnConnectionClosed(QuicConnectionId server_connection_id,
                                    QuicErrorCode error,
                                    const std::string& error_details,
                                    ConnectionCloseSource source) = 0;

    // Called when the session has become write blocked.
    virtual void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) = 0;

    // Called when the session receives reset on a stream from the peer.
    virtual void OnRstStreamReceived(const QuicRstStreamFrame& frame) = 0;

    // Called when the session receives a STOP_SENDING for a stream from the
    // peer.
    virtual void OnStopSendingReceived(const QuicStopSendingFrame& frame) = 0;

    // Called when on whether a NewConnectionId frame can been sent.
    virtual bool TryAddNewConnectionId(
        const QuicConnectionId& server_connection_id,
        const QuicConnectionId& new_connection_id) = 0;

    // Called when a ConnectionId has been retired.
    virtual void OnConnectionIdRetired(
        const QuicConnectionId& server_connection_id) = 0;

    virtual void OnServerPreferredAddressAvailable(
        const QuicSocketAddress& /*server_preferred_address*/) = 0;
  };

  // Does not take ownership of |connection| or |visitor|.
  QuicSession(QuicConnection* connection, Visitor* owner,
              const QuicConfig& config,
              const ParsedQuicVersionVector& supported_versions,
              QuicStreamCount num_expected_unidirectional_static_streams);
  QuicSession(QuicConnection* connection, Visitor* owner,
              const QuicConfig& config,
              const ParsedQuicVersionVector& supported_versions,
              QuicStreamCount num_expected_unidirectional_static_streams,
              std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer);
  QuicSession(const QuicSession&) = delete;
  QuicSession& operator=(const QuicSession&) = delete;

  ~QuicSession() override;

  virtual void Initialize();

  // Return the reserved crypto stream as a constant pointer.
  virtual const QuicCryptoStream* GetCryptoStream() const = 0;

  // QuicConnectionVisitorInterface methods:
  void OnStreamFrame(const QuicStreamFrame& frame) override;
  void OnCryptoFrame(const QuicCryptoFrame& frame) override;
  void OnRstStream(const QuicRstStreamFrame& frame) override;
  void OnGoAway(const QuicGoAwayFrame& frame) override;
  void OnMessageReceived(absl::string_view message) override;
  void OnHandshakeDoneReceived() override;
  void OnNewTokenReceived(absl::string_view token) override;
  void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override;
  void OnBlockedFrame(const QuicBlockedFrame& frame) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnWriteBlocked() override;
  void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& version) override;
  void OnPacketReceived(const QuicSocketAddress& self_address,
                        const QuicSocketAddress& peer_address,
                        bool is_connectivity_probe) override;
  void OnCanWrite() override;
  void OnCongestionWindowChange(QuicTime /*now*/) override {}
  void OnConnectionMigration(AddressChangeType /*type*/) override {}
  // Adds a connection level WINDOW_UPDATE frame.
  void OnAckNeedsRetransmittableFrame() override;
  void SendAckFrequency(const QuicAckFrequencyFrame& frame) override;
  void SendNewConnectionId(const QuicNewConnectionIdFrame& frame) override;
  void SendRetireConnectionId(uint64_t sequence_number) override;
  // Returns true if server_connection_id can be issued. If returns true,
  // |visitor_| may establish a mapping from |server_connection_id| to this
  // session, if that's not desired,
  // OnServerConnectionIdRetired(server_connection_id) can be used to remove the
  // mapping.
  bool MaybeReserveConnectionId(
      const QuicConnectionId& server_connection_id) override;
  void OnServerConnectionIdRetired(
      const QuicConnectionId& server_connection_id) override;
  bool WillingAndAbleToWrite() const override;
  std::string GetStreamsInfoForLogging() const override;
  void OnPathDegrading() override;
  void OnForwardProgressMadeAfterPathDegrading() override;
  bool AllowSelfAddressChange() const override;
  HandshakeState GetHandshakeState() const override;
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) override;
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override;
  void OnStopSendingFrame(const QuicStopSendingFrame& frame) override;
  void OnPacketDecrypted(EncryptionLevel level) override;
  void OnOneRttPacketAcknowledged() override;
  void OnHandshakePacketSent() override;
  void OnKeyUpdate(KeyUpdateReason /*reason*/) override {}
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override;
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override;
  void BeforeConnectionCloseSent() override {}
  bool ValidateToken(absl::string_view token) override;
  bool MaybeSendAddressToken() override;
  void OnBandwidthUpdateTimeout() override {}
  std::unique_ptr<QuicPathValidationContext> CreateContextForMultiPortPath()
      override {
    return nullptr;
  }
  void OnServerPreferredAddressAvailable(
      const QuicSocketAddress& /*server_preferred_address*/) override;

  // QuicStreamFrameDataProducer
  WriteStreamDataResult WriteStreamData(QuicStreamId id,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        QuicDataWriter* writer) override;
  bool WriteCryptoData(EncryptionLevel level, QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override;

  // SessionNotifierInterface methods:
  bool OnFrameAcked(const QuicFrame& frame, QuicTime::Delta ack_delay_time,
                    QuicTime receive_timestamp) override;
  void OnStreamFrameRetransmitted(const QuicStreamFrame& frame) override;
  void OnFrameLost(const QuicFrame& frame) override;
  bool RetransmitFrames(const QuicFrames& frames,
                        TransmissionType type) override;
  bool IsFrameOutstanding(const QuicFrame& frame) const override;
  bool HasUnackedCryptoData() const override;
  bool HasUnackedStreamData() const override;

  void SendMaxStreams(QuicStreamCount stream_count,
                      bool unidirectional) override;
  // The default implementation does nothing. Subclasses should override if
  // for example they queue up stream requests.
  virtual void OnCanCreateNewOutgoingStream(bool /*unidirectional*/) {}

  // Called on every incoming packet. Passes |packet| through to |connection_|.
  virtual void ProcessUdpPacket(const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address,
                                const QuicReceivedPacket& packet);

  // Sends |message| as a QUIC DATAGRAM frame (QUIC MESSAGE frame in gQUIC).
  // See <https://datatracker.ietf.org/doc/html/draft-ietf-quic-datagram> for
  // more details.
  //
  // Returns a MessageResult struct which includes the status of the write
  // operation and a message ID.  The message ID (not sent on the wire) can be
  // used to track the message; OnMessageAcked and OnMessageLost are called when
  // a specific message gets acked or lost.
  //
  // If the write operation is successful, all of the slices in |message| are
  // consumed, leaving them empty.  If MESSAGE_STATUS_INTERNAL_ERROR is
  // returned, the slices in question may or may not be consumed; it is no
  // longer safe to access those.  For all other status codes, |message| is kept
  // intact.
  //
  // Note that SendMessage will fail with status = MESSAGE_STATUS_BLOCKED
  // if the connection is congestion control blocked or the underlying socket is
  // write blocked. In this case the caller can retry sending message again when
  // connection becomes available, for example after getting OnCanWrite()
  // callback.
  //
  // SendMessage flushes the current packet even it is not full; if the
  // application needs to bundle other data in the same packet, consider using
  // QuicConnection::ScopedPacketFlusher around the relevant write operations.
  MessageResult SendMessage(absl::Span<quiche::QuicheMemSlice> message);

  // Same as above SendMessage, except caller can specify if the given |message|
  // should be flushed even if the underlying connection is deemed unwritable.
  MessageResult SendMessage(absl::Span<quiche::QuicheMemSlice> message,
                            bool flush);

  // Single-slice version of SendMessage().  Unlike the version above, this
  // version always takes ownership of the slice.
  MessageResult SendMessage(quiche::QuicheMemSlice message);

  // Called when message with |message_id| gets acked.
  virtual void OnMessageAcked(QuicMessageId message_id,
                              QuicTime receive_timestamp);

  // Called when message with |message_id| is considered as lost.
  virtual void OnMessageLost(QuicMessageId message_id);

  // QuicControlFrameManager::DelegateInterface
  // Close the connection on error.
  void OnControlFrameManagerError(QuicErrorCode error_code,
                                  std::string error_details) override;
  // Called by control frame manager when it wants to write control frames to
  // the peer. Returns true if |frame| is consumed, false otherwise. The frame
  // will be sent in specified transmission |type|.
  bool WriteControlFrame(const QuicFrame& frame,
                         TransmissionType type) override;

  // Called to send RST_STREAM (and STOP_SENDING) and close stream. If stream
  // |id| does not exist, just send RST_STREAM (and STOP_SENDING).
  virtual void ResetStream(QuicStreamId id, QuicRstStreamErrorCode error);

  // Called when the session wants to go away and not accept any new streams.
  virtual void SendGoAway(QuicErrorCode error_code, const std::string& reason);

  // Sends a BLOCKED frame.
  virtual void SendBlocked(QuicStreamId id, QuicStreamOffset byte_offset);

  // Sends a WINDOW_UPDATE frame.
  virtual void SendWindowUpdate(QuicStreamId id, QuicStreamOffset byte_offset);

  // Called by stream |stream_id| when it gets closed.
  virtual void OnStreamClosed(QuicStreamId stream_id);

  // Returns true if outgoing packets will be encrypted, even if the server
  // hasn't confirmed the handshake yet.
  virtual bool IsEncryptionEstablished() const;

  // Returns true if 1RTT keys are available.
  bool OneRttKeysAvailable() const;

  // Called by the QuicCryptoStream when a new QuicConfig has been negotiated.
  virtual void OnConfigNegotiated();

  // Called by the TLS handshaker when ALPS data is received.
  // Returns an error message if an error has occurred, or nullopt otherwise.
  virtual absl::optional<std::string> OnAlpsData(const uint8_t* alps_data,
                                                 size_t alps_length);

  // From HandshakerDelegateInterface
  bool OnNewDecryptionKeyAvailable(EncryptionLevel level,
                                   std::unique_ptr<QuicDecrypter> decrypter,
                                   bool set_alternative_decrypter,
                                   bool latch_once_used) override;
  void OnNewEncryptionKeyAvailable(
      EncryptionLevel level, std::unique_ptr<QuicEncrypter> encrypter) override;
  void SetDefaultEncryptionLevel(EncryptionLevel level) override;
  void OnTlsHandshakeComplete() override;
  void DiscardOldDecryptionKey(EncryptionLevel level) override;
  void DiscardOldEncryptionKey(EncryptionLevel level) override;
  void NeuterUnencryptedData() override;
  void NeuterHandshakeData() override;
  void OnZeroRttRejected(int reason) override;
  bool FillTransportParameters(TransportParameters* params) override;
  QuicErrorCode ProcessTransportParameters(const TransportParameters& params,
                                           bool is_resumption,
                                           std::string* error_details) override;
  void OnHandshakeCallbackDone() override;
  bool PacketFlusherAttached() const override;
  ParsedQuicVersion parsed_version() const override { return version(); }

  // Implement StreamDelegateInterface.
  void OnStreamError(QuicErrorCode error_code,
                     std::string error_details) override;
  void OnStreamError(QuicErrorCode error_code,
                     QuicIetfTransportErrorCodes ietf_error,
                     std::string error_details) override;
  // Sets priority in the write blocked list.
  void RegisterStreamPriority(QuicStreamId id, bool is_static,
                              const QuicStreamPriority& priority) override;
  // Clears priority from the write blocked list.
  void UnregisterStreamPriority(QuicStreamId id) override;
  // Updates priority on the write blocked list.
  void UpdateStreamPriority(QuicStreamId id,
                            const QuicStreamPriority& new_priority) override;

  // Called by streams when they want to write data to the peer.
  // Returns a pair with the number of bytes consumed from data, and a boolean
  // indicating if the fin bit was consumed.  This does not indicate the data
  // has been sent on the wire: it may have been turned into a packet and queued
  // if the socket was unexpectedly blocked.
  QuicConsumedData WritevData(QuicStreamId id, size_t write_length,
                              QuicStreamOffset offset, StreamSendingState state,
                              TransmissionType type,
                              EncryptionLevel level) override;

  size_t SendCryptoData(EncryptionLevel level, size_t write_length,
                        QuicStreamOffset offset,
                        TransmissionType type) override;

  // Called by the QuicCryptoStream when a handshake message is sent.
  virtual void OnCryptoHandshakeMessageSent(
      const CryptoHandshakeMessage& message);

  // Called by the QuicCryptoStream when a handshake message is received.
  virtual void OnCryptoHandshakeMessageReceived(
      const CryptoHandshakeMessage& message);

  // Returns mutable config for this session. Returned config is owned
  // by QuicSession.
  QuicConfig* config() { return &config_; }
  const QuicConfig* config() const { return &config_; }

  // Returns true if the stream existed previously and has been closed.
  // Returns false if the stream is still active or if the stream has
  // not yet been created.
  bool IsClosedStream(QuicStreamId id);

  QuicConnection* connection() { return connection_; }
  const QuicConnection* connection() const { return connection_; }
  const QuicSocketAddress& peer_address() const {
    return connection_->peer_address();
  }
  const QuicSocketAddress& self_address() const {
    return connection_->self_address();
  }
  QuicConnectionId connection_id() const {
    return connection_->connection_id();
  }

  // Returns the number of currently open streams, excluding static streams, and
  // never counting unfinished streams.
  size_t GetNumActiveStreams() const;

  // Add the stream to the session's write-blocked list because it is blocked by
  // connection-level flow control but not by its own stream-level flow control.
  // The stream will be given a chance to write when a connection-level
  // WINDOW_UPDATE arrives.
  virtual void MarkConnectionLevelWriteBlocked(QuicStreamId id);

  // Called to close zombie stream |id|.
  void MaybeCloseZombieStream(QuicStreamId id);

  // Returns true if there is pending handshake data in the crypto stream.
  // TODO(ianswett): Make this private or remove.
  bool HasPendingHandshake() const;

  // Returns true if the session has data to be sent, either queued in the
  // connection, or in a write-blocked stream.
  bool HasDataToWrite() const;

  // Initiates a path validation on the path described in the given context,
  // asynchronously calls |result_delegate| upon success or failure.
  // The initiator should extend QuicPathValidationContext to provide the writer
  // and ResultDelegate to react upon the validation result.
  // Example implementations of these for path validation for connection
  // migration could be:
  //  class QUIC_EXPORT_PRIVATE PathMigrationContext
  //      : public QuicPathValidationContext {
  //   public:
  //    PathMigrationContext(std::unique_ptr<QuicPacketWriter> writer,
  //                         const QuicSocketAddress& self_address,
  //                         const QuicSocketAddress& peer_address)
  //        : QuicPathValidationContext(self_address, peer_address),
  //          alternative_writer_(std::move(writer)) {}
  //
  //    QuicPacketWriter* WriterToUse() override {
  //         return alternative_writer_.get();
  //    }
  //
  //    QuicPacketWriter* ReleaseWriter() {
  //         return alternative_writer_.release();
  //    }
  //
  //   private:
  //    std::unique_ptr<QuicPacketWriter> alternative_writer_;
  //  };
  //
  //  class PathMigrationValidationResultDelegate
  //      : public QuicPathValidator::ResultDelegate {
  //   public:
  //    PathMigrationValidationResultDelegate(QuicConnection* connection)
  //        : QuicPathValidator::ResultDelegate(), connection_(connection) {}
  //
  //    void OnPathValidationSuccess(
  //        std::unique_ptr<QuicPathValidationContext> context) override {
  //    // Do some work to prepare for migration.
  //    // ...
  //
  //    // Actually migrate to the validated path.
  //    auto migration_context = std::unique_ptr<PathMigrationContext>(
  //        static_cast<PathMigrationContext*>(context.release()));
  //    connection_->MigratePath(migration_context->self_address(),
  //                          migration_context->peer_address(),
  //                          migration_context->ReleaseWriter(),
  //                          /*owns_writer=*/true);
  //
  //    // Post-migration actions
  //    // ...
  //  }
  //
  //    void OnPathValidationFailure(
  //        std::unique_ptr<QuicPathValidationContext> /*context*/) override {
  //    // Handle validation failure.
  //  }
  //
  //   private:
  //    QuicConnection* connection_;
  //  };
  void ValidatePath(
      std::unique_ptr<QuicPathValidationContext> context,
      std::unique_ptr<QuicPathValidator::ResultDelegate> result_delegate,
      PathValidationReason reason);

  // Return true if there is a path being validated.
  bool HasPendingPathValidation() const;

  // Switch to the path described in |context| without validating the path.
  bool MigratePath(const QuicSocketAddress& self_address,
                   const QuicSocketAddress& peer_address,
                   QuicPacketWriter* writer, bool owns_writer);

  // Returns the largest payload that will fit into a single MESSAGE frame.
  // Because overhead can vary during a connection, this method should be
  // checked for every message.
  QuicPacketLength GetCurrentLargestMessagePayload() const;

  // Returns the largest payload that will fit into a single MESSAGE frame at
  // any point during the connection.  This assumes the version and
  // connection ID lengths do not change.
  QuicPacketLength GetGuaranteedLargestMessagePayload() const;

  bool transport_goaway_sent() const { return transport_goaway_sent_; }

  bool transport_goaway_received() const { return transport_goaway_received_; }

  // Returns the Google QUIC error code
  QuicErrorCode error() const { return on_closed_frame_.quic_error_code; }
  const std::string& error_details() const {
    return on_closed_frame_.error_details;
  }
  uint64_t transport_close_frame_type() const {
    return on_closed_frame_.transport_close_frame_type;
  }
  QuicConnectionCloseType close_type() const {
    return on_closed_frame_.close_type;
  }

  Perspective perspective() const { return perspective_; }

  QuicFlowController* flow_controller() { return &flow_controller_; }

  // Returns true if connection is flow controller blocked.
  bool IsConnectionFlowControlBlocked() const;

  // Returns true if any stream is flow controller blocked.
  bool IsStreamFlowControlBlocked();

  size_t max_open_incoming_bidirectional_streams() const;
  size_t max_open_incoming_unidirectional_streams() const;

  size_t MaxAvailableBidirectionalStreams() const;
  size_t MaxAvailableUnidirectionalStreams() const;

  // Returns existing stream with id = |stream_id|. If no
  // such stream exists, and |stream_id| is a peer-created stream id,
  // then a new stream is created and returned. In all other cases, nullptr is
  // returned.
  // Caller does not own the returned stream.
  QuicStream* GetOrCreateStream(const QuicStreamId stream_id);

  // Mark a stream as draining.
  void StreamDraining(QuicStreamId id, bool unidirectional);

  // Returns true if this stream should yield writes to another blocked stream.
  virtual bool ShouldYield(QuicStreamId stream_id);

  // Clean up closed_streams_.
  void CleanUpClosedStreams();

  const ParsedQuicVersionVector& supported_versions() const {
    return supported_versions_;
  }

  QuicStreamId next_outgoing_bidirectional_stream_id() const;
  QuicStreamId next_outgoing_unidirectional_stream_id() const;

  // Return true if given stream is peer initiated.
  bool IsIncomingStream(QuicStreamId id) const;

  // Record errors when a connection is closed at the server side, should only
  // be called from server's perspective.
  // Noop if |error| is QUIC_NO_ERROR.
  static void RecordConnectionCloseAtServer(QuicErrorCode error,
                                            ConnectionCloseSource source);

  QuicTransportVersion transport_version() const {
    return connection_->transport_version();
  }

  ParsedQuicVersion version() const { return connection_->version(); }

  bool is_configured() const { return is_configured_; }

  // Called to neuter crypto data of encryption |level|.
  void NeuterCryptoDataOfEncryptionLevel(EncryptionLevel level);

  // Returns the ALPN values to negotiate on this session.
  virtual std::vector<std::string> GetAlpnsToOffer() const {
    // TODO(vasilvv): this currently sets HTTP/3 by default.  Switch all
    // non-HTTP applications to appropriate ALPNs.
    return std::vector<std::string>({AlpnForVersion(connection()->version())});
  }

  // Provided a list of ALPNs offered by the client, selects an ALPN from the
  // list, or alpns.end() if none of the ALPNs are acceptable.
  virtual std::vector<absl::string_view>::const_iterator SelectAlpn(
      const std::vector<absl::string_view>& alpns) const;

  // Called when the ALPN of the connection is established for a connection that
  // uses TLS handshake.
  virtual void OnAlpnSelected(absl::string_view alpn);

  // Called on clients by the crypto handshaker to provide application state
  // necessary for sending application data in 0-RTT. The state provided here is
  // the same state that was provided to the crypto handshaker in
  // QuicCryptoStream::SetServerApplicationStateForResumption on a previous
  // connection. Application protocols that require state to be carried over
  // from the previous connection to support 0-RTT data must implement this
  // method to ingest this state. For example, an HTTP/3 QuicSession would
  // implement this function to process the remembered server SETTINGS and apply
  // those SETTINGS to 0-RTT data. This function returns true if the application
  // state has been successfully processed, and false if there was an error
  // processing the cached state and the connection should be closed.
  virtual bool ResumeApplicationState(ApplicationState* /*cached_state*/) {
    return true;
  }

  // Does actual work of sending RESET_STREAM, if the stream type allows.
  // Also informs the connection so that pending stream frames can be flushed.
  virtual void MaybeSendRstStreamFrame(QuicStreamId id,
                                       QuicResetStreamError error,
                                       QuicStreamOffset bytes_written);

  // Sends a STOP_SENDING frame if the stream type allows.
  virtual void MaybeSendStopSendingFrame(QuicStreamId id,
                                         QuicResetStreamError error);

  // Returns the encryption level to send application data.
  EncryptionLevel GetEncryptionLevelToSendApplicationData() const;

  const absl::optional<std::string> user_agent_id() const {
    return user_agent_id_;
  }

  // TODO(wub): remove saving user-agent to QuicSession.
  void SetUserAgentId(std::string user_agent_id) {
    user_agent_id_ = std::move(user_agent_id);
    connection()->OnUserAgentIdKnown(user_agent_id_.value());
  }

  void SetSourceAddressTokenToSend(absl::string_view token) {
    connection()->SetSourceAddressTokenToSend(token);
  }

  const QuicClock* GetClock() const {
    return connection()->helper()->GetClock();
  }

  bool liveness_testing_in_progress() const {
    return liveness_testing_in_progress_;
  }

  virtual QuicSSLConfig GetSSLConfig() const { return QuicSSLConfig(); }

  // Try converting all pending streams to normal streams.
  void ProcessAllPendingStreams();

  const ParsedQuicVersionVector& client_original_supported_versions() const {
    QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
    return client_original_supported_versions_;
  }
  void set_client_original_supported_versions(
      const ParsedQuicVersionVector& client_original_supported_versions) {
    QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
    client_original_supported_versions_ = client_original_supported_versions;
  }

  // Controls whether the default datagram queue used by the session actually
  // queues the datagram.  If set to true, the datagrams in the default queue
  // will be forcefully flushed, potentially bypassing congestion control and
  // other limitations.
  void SetForceFlushForDefaultQueue(bool force_flush) {
    datagram_queue_.SetForceFlush(force_flush);
  }

 protected:
  using StreamMap =
      absl::flat_hash_map<QuicStreamId, std::unique_ptr<QuicStream>>;

  using PendingStreamMap =
      absl::flat_hash_map<QuicStreamId, std::unique_ptr<PendingStream>>;

  using ClosedStreams = std::vector<std::unique_ptr<QuicStream>>;

  using ZombieStreamMap =
      absl::flat_hash_map<QuicStreamId, std::unique_ptr<QuicStream>>;

  std::string on_closed_frame_string() const;

  // Creates a new stream to handle a peer-initiated stream.
  // Caller does not own the returned stream.
  // Returns nullptr and does error handling if the stream can not be created.
  virtual QuicStream* CreateIncomingStream(QuicStreamId id) = 0;
  virtual QuicStream* CreateIncomingStream(PendingStream* pending) = 0;

  // Return the reserved crypto stream.
  virtual QuicCryptoStream* GetMutableCryptoStream() = 0;

  // Adds |stream| to the stream map.
  virtual void ActivateStream(std::unique_ptr<QuicStream> stream);

  // Set transmission type of next sending packets.
  void SetTransmissionType(TransmissionType type);

  // Returns the stream ID for a new outgoing bidirectional/unidirectional
  // stream, and increments the underlying counter.
  QuicStreamId GetNextOutgoingBidirectionalStreamId();
  QuicStreamId GetNextOutgoingUnidirectionalStreamId();

  // Indicates whether the next outgoing bidirectional/unidirectional stream ID
  // can be allocated or not. The test for version-99/IETF QUIC is whether it
  // will exceed the maximum-stream-id or not. For non-version-99 (Google) QUIC
  // it checks whether the next stream would exceed the limit on the number of
  // open streams.
  bool CanOpenNextOutgoingBidirectionalStream();
  bool CanOpenNextOutgoingUnidirectionalStream();

  // Returns the maximum bidirectional streams parameter sent with the handshake
  // as a transport parameter, or in the most recent MAX_STREAMS frame.
  QuicStreamCount GetAdvertisedMaxIncomingBidirectionalStreams() const;

  // When a stream is closed locally, it may not yet know how many bytes the
  // peer sent on that stream.
  // When this data arrives (via stream frame w. FIN, trailing headers, or RST)
  // this method is called, and correctly updates the connection level flow
  // controller.
  virtual void OnFinalByteOffsetReceived(QuicStreamId id,
                                         QuicStreamOffset final_byte_offset);

  // Returns true if a frame with the given type and id can be prcoessed by a
  // PendingStream. However, the frame will always be processed by a QuicStream
  // if one exists with the given stream_id.
  virtual bool UsesPendingStreamForFrame(QuicFrameType /*type*/,
                                         QuicStreamId /*stream_id*/) const {
    return false;
  }

  // Returns true if a pending stream should be converted to a real stream after
  // a corresponding STREAM_FRAME is received.
  virtual bool ShouldProcessPendingStreamImmediately() const { return true; }

  spdy::SpdyPriority GetSpdyPriorityofStream(QuicStreamId stream_id) const {
    return write_blocked_streams_.GetPriorityofStream(stream_id).urgency;
  }

  size_t pending_streams_size() const { return pending_stream_map_.size(); }

  ClosedStreams* closed_streams() { return &closed_streams_; }

  void set_largest_peer_created_stream_id(
      QuicStreamId largest_peer_created_stream_id);

  QuicWriteBlockedList* write_blocked_streams() {
    return &write_blocked_streams_;
  }

  // Returns true if the stream is still active.
  bool IsOpenStream(QuicStreamId id);

  // Returns true if the stream is a static stream.
  bool IsStaticStream(QuicStreamId id) const;

  // Close connection when receive a frame for a locally-created nonexistent
  // stream.
  // Prerequisite: IsClosedStream(stream_id) == false
  // Server session might need to override this method to allow server push
  // stream to be promised before creating an active stream.
  virtual void HandleFrameOnNonexistentOutgoingStream(QuicStreamId stream_id);

  virtual bool MaybeIncreaseLargestPeerStreamId(const QuicStreamId stream_id);

  void InsertLocallyClosedStreamsHighestOffset(const QuicStreamId id,
                                               QuicStreamOffset offset);
  // If stream is a locally closed stream, this RST will update FIN offset.
  // Otherwise stream is a preserved stream and the behavior of it depends on
  // derived class's own implementation.
  virtual void HandleRstOnValidNonexistentStream(
      const QuicRstStreamFrame& frame);

  // Returns a stateless reset token which will be included in the public reset
  // packet.
  virtual StatelessResetToken GetStatelessResetToken() const;

  QuicControlFrameManager& control_frame_manager() {
    return control_frame_manager_;
  }

  const LegacyQuicStreamIdManager& stream_id_manager() const {
    return stream_id_manager_;
  }

  QuicDatagramQueue* datagram_queue() { return &datagram_queue_; }

  size_t num_static_streams() const { return num_static_streams_; }

  size_t num_zombie_streams() const { return num_zombie_streams_; }

  bool was_zero_rtt_rejected() const { return was_zero_rtt_rejected_; }

  size_t num_outgoing_draining_streams() const {
    return num_outgoing_draining_streams_;
  }

  size_t num_draining_streams() const { return num_draining_streams_; }

  // Processes the stream type information of |pending| depending on
  // different kinds of sessions' own rules. If the pending stream has been
  // converted to a normal stream, returns a pointer to the new stream;
  // otherwise, returns nullptr.
  virtual QuicStream* ProcessPendingStream(PendingStream* /*pending*/) {
    return nullptr;
  }

  // Called by applications to perform |action| on active streams.
  // Stream iteration will be stopped if action returns false.
  void PerformActionOnActiveStreams(std::function<bool(QuicStream*)> action);
  void PerformActionOnActiveStreams(
      std::function<bool(QuicStream*)> action) const;

  // Return the largest peer created stream id depending on directionality
  // indicated by |unidirectional|.
  QuicStreamId GetLargestPeerCreatedStreamId(bool unidirectional) const;

  // Deletes the connection and sets it to nullptr, so calling it mulitiple
  // times is safe.
  void DeleteConnection();

  // Call SetPriority() on stream id |id| and return true if stream is active.
  bool MaybeSetStreamPriority(QuicStreamId stream_id,
                              const QuicStreamPriority& priority);

  void SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface> tuner) {
    connection()->SetLossDetectionTuner(std::move(tuner));
  }

  // Find stream with |id|, returns nullptr if the stream does not exist or
  // closed. static streams and zombie streams are not considered active
  // streams.
  QuicStream* GetActiveStream(QuicStreamId id) const;

  const UberQuicStreamIdManager& ietf_streamid_manager() const {
    QUICHE_DCHECK(VersionHasIetfQuicFrames(transport_version()));
    return ietf_streamid_manager_;
  }

  // Only called at a server session. Generate a CachedNetworkParameters that
  // can be sent to the client as part of the address token, based on the latest
  // bandwidth/rtt information. If return absl::nullopt, address token will not
  // contain the CachedNetworkParameters.
  virtual absl::optional<CachedNetworkParameters>
  GenerateCachedNetworkParameters() const {
    return absl::nullopt;
  }

 private:
  friend class test::QuicSessionPeer;

  // Called in OnConfigNegotiated when we receive a new stream level flow
  // control window in a negotiated config. Closes the connection if invalid.
  void OnNewStreamFlowControlWindow(QuicStreamOffset new_window);

  // Called in OnConfigNegotiated when we receive a new unidirectional stream
  // flow control window in a negotiated config.
  void OnNewStreamUnidirectionalFlowControlWindow(QuicStreamOffset new_window);

  // Called in OnConfigNegotiated when we receive a new outgoing bidirectional
  // stream flow control window in a negotiated config.
  void OnNewStreamOutgoingBidirectionalFlowControlWindow(
      QuicStreamOffset new_window);

  // Called in OnConfigNegotiated when we receive a new incoming bidirectional
  // stream flow control window in a negotiated config.
  void OnNewStreamIncomingBidirectionalFlowControlWindow(
      QuicStreamOffset new_window);

  // Called in OnConfigNegotiated when we receive a new connection level flow
  // control window in a negotiated config. Closes the connection if invalid.
  void OnNewSessionFlowControlWindow(QuicStreamOffset new_window);

  // Debug helper for |OnCanWrite()|, check that OnStreamWrite() makes
  // forward progress.  Returns false if busy loop detected.
  bool CheckStreamNotBusyLooping(QuicStream* stream,
                                 uint64_t previous_bytes_written,
                                 bool previous_fin_sent);

  // Debug helper for OnCanWrite. Check that after QuicStream::OnCanWrite(),
  // if stream has buffered data and is not stream level flow control blocked,
  // it has to be in the write blocked list.
  bool CheckStreamWriteBlocked(QuicStream* stream) const;

  // Called in OnConfigNegotiated for Finch trials to measure performance of
  // starting with larger flow control receive windows.
  void AdjustInitialFlowControlWindows(size_t stream_window);

  // Find stream with |id|, returns nullptr if the stream does not exist or
  // closed.
  QuicStream* GetStream(QuicStreamId id) const;

  // Can return NULL, e.g., if the stream has been closed before.
  PendingStream* GetOrCreatePendingStream(QuicStreamId stream_id);

  // Let streams and control frame managers retransmit lost data, returns true
  // if all lost data is retransmitted. Returns false otherwise.
  bool RetransmitLostData();

  // Returns true if stream data should be written.
  bool CanWriteStreamData() const;

  // Closes the pending stream |stream_id| before it has been created.
  void ClosePendingStream(QuicStreamId stream_id);

  // Whether the frame with given type and id should be feed to a pending
  // stream.
  bool ShouldProcessFrameByPendingStream(QuicFrameType type,
                                         QuicStreamId id) const;

  // Process the pending stream if possible.
  void MaybeProcessPendingStream(PendingStream* pending);

  // Creates or gets pending stream, feeds it with |frame|, and returns the
  // pending stream. Can return NULL, e.g., if the stream ID is invalid.
  PendingStream* PendingStreamOnStreamFrame(const QuicStreamFrame& frame);

  // Creates or gets pending strea, feed it with |frame|, and closes the pending
  // stream.
  void PendingStreamOnRstStream(const QuicRstStreamFrame& frame);

  // Creates or gets pending stream, feeds it with |frame|, and records the
  // max_data in the pending stream.
  void PendingStreamOnWindowUpdateFrame(const QuicWindowUpdateFrame& frame);

  // Creates or gets pending stream, feeds it with |frame|, and records the
  // ietf_error_code in the pending stream.
  void PendingStreamOnStopSendingFrame(const QuicStopSendingFrame& frame);

  // Keep track of highest received byte offset of locally closed streams, while
  // waiting for a definitive final highest offset from the peer.
  absl::flat_hash_map<QuicStreamId, QuicStreamOffset>
      locally_closed_streams_highest_offset_;

  QuicConnection* connection_;

  // Store perspective on QuicSession during the constructor as it may be needed
  // during our destructor when connection_ may have already been destroyed.
  Perspective perspective_;

  // May be null.
  Visitor* visitor_;

  // A list of streams which need to write more data.  Stream register
  // themselves in their constructor, and unregisterm themselves in their
  // destructors, so the write blocked list must outlive all streams.
  QuicWriteBlockedList write_blocked_streams_;

  ClosedStreams closed_streams_;

  QuicConfig config_;

  // Map from StreamId to pointers to streams. Owns the streams.
  StreamMap stream_map_;

  // Map from StreamId to PendingStreams for peer-created unidirectional streams
  // which are waiting for the first byte of payload to arrive.
  PendingStreamMap pending_stream_map_;

  // TODO(fayang): Consider moving LegacyQuicStreamIdManager into
  // UberQuicStreamIdManager.
  // Manages stream IDs for Google QUIC.
  LegacyQuicStreamIdManager stream_id_manager_;

  // Manages stream IDs for version99/IETF QUIC
  UberQuicStreamIdManager ietf_streamid_manager_;

  // A counter for streams which have sent and received FIN but waiting for
  // application to consume data.
  size_t num_draining_streams_;

  // A counter for self initiated streams which have sent and received FIN but
  // waiting for application to consume data.
  size_t num_outgoing_draining_streams_;

  // A counter for static streams which are in stream_map_.
  size_t num_static_streams_;

  // A counter for streams which have done reading and writing, but are waiting
  // for acks.
  size_t num_zombie_streams_;

  // Received information for a connection close.
  QuicConnectionCloseFrame on_closed_frame_;
  absl::optional<ConnectionCloseSource> source_;

  // Used for connection-level flow control.
  QuicFlowController flow_controller_;

  // The stream id which was last popped in OnCanWrite, or 0, if not under the
  // call stack of OnCanWrite.
  QuicStreamId currently_writing_stream_id_;

  // Whether a transport layer GOAWAY frame has been sent.
  // Such a frame only exists in Google QUIC, therefore |transport_goaway_sent_|
  // is always false when using IETF QUIC.
  bool transport_goaway_sent_;

  // Whether a transport layer GOAWAY frame has been received.
  // Such a frame only exists in Google QUIC, therefore
  // |transport_goaway_received_| is always false when using IETF QUIC.
  bool transport_goaway_received_;

  QuicControlFrameManager control_frame_manager_;

  // Id of latest successfully sent message.
  QuicMessageId last_message_id_;

  // The buffer used to queue the DATAGRAM frames.
  QuicDatagramQueue datagram_queue_;

  // TODO(fayang): switch to linked_hash_set when chromium supports it. The bool
  // is not used here.
  // List of streams with pending retransmissions.
  quiche::QuicheLinkedHashMap<QuicStreamId, bool>
      streams_with_pending_retransmission_;

  // Clean up closed_streams_ when this alarm fires.
  std::unique_ptr<QuicAlarm> closed_streams_clean_up_alarm_;

  // Supported version list used by the crypto handshake only. Please note, this
  // list may be a superset of the connection framer's supported versions.
  ParsedQuicVersionVector supported_versions_;

  // Only non-empty on the client after receiving a version negotiation packet,
  // contains the configured versions from the original session before version
  // negotiation was received.
  ParsedQuicVersionVector client_original_supported_versions_;

  absl::optional<std::string> user_agent_id_;

  // Initialized to false. Set to true when the session has been properly
  // configured and is ready for general operation.
  bool is_configured_;

  // Whether the session has received a 0-RTT rejection (QUIC+TLS only).
  bool was_zero_rtt_rejected_;

  // This indicates a liveness testing is in progress, and push back the
  // creation of new outgoing bidirectional streams.
  bool liveness_testing_in_progress_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SESSION_H_
