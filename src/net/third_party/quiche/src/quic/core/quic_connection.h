// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The entity that handles framing writes for a Quic client or server.
// Each QuicSession will have a connection associated with it.
//
// On the server side, the Dispatcher handles the raw reads, and hands off
// packets via ProcessUdpPacket for framing and processing.
//
// On the client side, the Connection handles the raw reads, as well as the
// processing.
//
// Note: this class is not thread-safe.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_max_streams_frame.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_blocked_writer_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_idle_network_detector.h"
#include "net/third_party/quiche/src/quic/core/quic_mtu_discovery.h"
#include "net/third_party/quiche/src/quic/core/quic_network_blackhole_detector.h"
#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_sent_packet_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/uber_received_packet_manager.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicClock;
class QuicConfig;
class QuicConnection;
class QuicRandom;

namespace test {
class QuicConnectionPeer;
}  // namespace test

// Class that receives callbacks from the connection when frames are received
// and when other interesting events happen.
class QUIC_EXPORT_PRIVATE QuicConnectionVisitorInterface {
 public:
  virtual ~QuicConnectionVisitorInterface() {}

  // A simple visitor interface for dealing with a data frame.
  virtual void OnStreamFrame(const QuicStreamFrame& frame) = 0;

  // Called when a CRYPTO frame containing handshake data is received.
  virtual void OnCryptoFrame(const QuicCryptoFrame& frame) = 0;

  // The session should process the WINDOW_UPDATE frame, adjusting both stream
  // and connection level flow control windows.
  virtual void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) = 0;

  // A BLOCKED frame indicates the peer is flow control blocked
  // on a specified stream.
  virtual void OnBlockedFrame(const QuicBlockedFrame& frame) = 0;

  // Called when the stream is reset by the peer.
  virtual void OnRstStream(const QuicRstStreamFrame& frame) = 0;

  // Called when the connection is going away according to the peer.
  virtual void OnGoAway(const QuicGoAwayFrame& frame) = 0;

  // Called when |message| has been received.
  virtual void OnMessageReceived(quiche::QuicheStringPiece message) = 0;

  // Called when a HANDSHAKE_DONE frame has been received.
  virtual void OnHandshakeDoneReceived() = 0;

  // Called when a MAX_STREAMS frame has been received from the peer.
  virtual bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) = 0;

  // Called when a STREAMS_BLOCKED frame has been received from the peer.
  virtual bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) = 0;

  // Called when the connection is closed either locally by the framer, or
  // remotely by the peer.
  virtual void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                                  ConnectionCloseSource source) = 0;

  // Called when the connection failed to write because the socket was blocked.
  virtual void OnWriteBlocked() = 0;

  // Called once a specific QUIC version is agreed by both endpoints.
  virtual void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& version) = 0;

  // Called when a packet has been received by the connection, after being
  // validated and parsed. Only called when the client receives a valid packet
  // or the server receives a connectivity probing packet.
  // |is_connectivity_probe| is true if the received packet is a connectivity
  // probe.
  virtual void OnPacketReceived(const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address,
                                bool is_connectivity_probe) = 0;

  // Called when a blocked socket becomes writable.
  virtual void OnCanWrite() = 0;

  // Called when the connection needs more data to probe for additional
  // bandwidth.  Returns true if data was sent, false otherwise.
  virtual bool SendProbingData() = 0;

  // Called when the connection experiences a change in congestion window.
  virtual void OnCongestionWindowChange(QuicTime now) = 0;

  // Called when the connection receives a packet from a migrated client.
  virtual void OnConnectionMigration(AddressChangeType type) = 0;

  // Called when the peer seems unreachable over the current path.
  virtual void OnPathDegrading() = 0;

  // Called when the connection sends ack after
  // max_consecutive_num_packets_with_no_retransmittable_frames_ consecutive not
  // retransmittable packets sent. To instigate an ack from peer, a
  // retransmittable frame needs to be added.
  virtual void OnAckNeedsRetransmittableFrame() = 0;

  // Called when a ping needs to be sent.
  virtual void SendPing() = 0;

  // Called to ask if the visitor wants to schedule write resumption as it both
  // has pending data to write, and is able to write (e.g. based on flow control
  // limits).
  // Writes may be pending because they were write-blocked, congestion-throttled
  // or yielded to other connections.
  virtual bool WillingAndAbleToWrite() const = 0;

  // Called to ask if any handshake messages are pending in this visitor.
  virtual bool HasPendingHandshake() const = 0;

  // Called to ask if the connection should be kept alive and prevented
  // from timing out, for example if there are outstanding application
  // transactions expecting a response.
  virtual bool ShouldKeepConnectionAlive() const = 0;

  // Called when a self address change is observed. Returns true if self address
  // change is allowed.
  virtual bool AllowSelfAddressChange() const = 0;

  // Called to get current handshake state.
  virtual HandshakeState GetHandshakeState() const = 0;

  // Called when an ACK is received with a larger |largest_acked| than
  // previously observed.
  virtual void OnForwardProgressConfirmed() = 0;

  // Called when a STOP_SENDING frame has been received.
  virtual void OnStopSendingFrame(const QuicStopSendingFrame& frame) = 0;

  // Called when a packet of encryption |level| has been successfully decrypted.
  virtual void OnPacketDecrypted(EncryptionLevel level) = 0;

  // Called when a 1RTT packet has been acknowledged.
  virtual void OnOneRttPacketAcknowledged() = 0;
};

// Interface which gets callbacks from the QuicConnection at interesting
// points.  Implementations must not mutate the state of the connection
// as a result of these callbacks.
class QUIC_EXPORT_PRIVATE QuicConnectionDebugVisitor
    : public QuicSentPacketManager::DebugDelegate {
 public:
  ~QuicConnectionDebugVisitor() override {}

  // Called when a packet has been sent.
  virtual void OnPacketSent(const SerializedPacket& /*serialized_packet*/,
                            TransmissionType /*transmission_type*/,
                            QuicTime /*sent_time*/) {}

  // Called when a coalesced packet has been sent.
  virtual void OnCoalescedPacketSent(
      const QuicCoalescedPacket& /*coalesced_packet*/,
      size_t /*length*/) {}

  // Called when a PING frame has been sent.
  virtual void OnPingSent() {}

  // Called when a packet has been received, but before it is
  // validated or parsed.
  virtual void OnPacketReceived(const QuicSocketAddress& /*self_address*/,
                                const QuicSocketAddress& /*peer_address*/,
                                const QuicEncryptedPacket& /*packet*/) {}

  // Called when the unauthenticated portion of the header has been parsed.
  virtual void OnUnauthenticatedHeader(const QuicPacketHeader& /*header*/) {}

  // Called when a packet is received with a connection id that does not
  // match the ID of this connection.
  virtual void OnIncorrectConnectionId(QuicConnectionId /*connection_id*/) {}

  // Called when an undecryptable packet has been received.
  virtual void OnUndecryptablePacket() {}

  // Called when a duplicate packet has been received.
  virtual void OnDuplicatePacket(QuicPacketNumber /*packet_number*/) {}

  // Called when the protocol version on the received packet doensn't match
  // current protocol version of the connection.
  virtual void OnProtocolVersionMismatch(ParsedQuicVersion /*version*/) {}

  // Called when the complete header of a packet has been parsed.
  virtual void OnPacketHeader(const QuicPacketHeader& /*header*/) {}

  // Called when a StreamFrame has been parsed.
  virtual void OnStreamFrame(const QuicStreamFrame& /*frame*/) {}

  // Called when a CRYPTO frame containing handshake data is received.
  virtual void OnCryptoFrame(const QuicCryptoFrame& /*frame*/) {}

  // Called when a StopWaitingFrame has been parsed.
  virtual void OnStopWaitingFrame(const QuicStopWaitingFrame& /*frame*/) {}

  // Called when a QuicPaddingFrame has been parsed.
  virtual void OnPaddingFrame(const QuicPaddingFrame& /*frame*/) {}

  // Called when a Ping has been parsed.
  virtual void OnPingFrame(const QuicPingFrame& /*frame*/) {}

  // Called when a GoAway has been parsed.
  virtual void OnGoAwayFrame(const QuicGoAwayFrame& /*frame*/) {}

  // Called when a RstStreamFrame has been parsed.
  virtual void OnRstStreamFrame(const QuicRstStreamFrame& /*frame*/) {}

  // Called when a ConnectionCloseFrame has been parsed. All forms
  // of CONNECTION CLOSE are handled, Google QUIC, IETF QUIC
  // CONNECTION CLOSE/Transport and IETF QUIC CONNECTION CLOSE/Application
  virtual void OnConnectionCloseFrame(
      const QuicConnectionCloseFrame& /*frame*/) {}

  // Called when a WindowUpdate has been parsed.
  virtual void OnWindowUpdateFrame(const QuicWindowUpdateFrame& /*frame*/,
                                   const QuicTime& /*receive_time*/) {}

  // Called when a BlockedFrame has been parsed.
  virtual void OnBlockedFrame(const QuicBlockedFrame& /*frame*/) {}

  // Called when a NewConnectionIdFrame has been parsed.
  virtual void OnNewConnectionIdFrame(
      const QuicNewConnectionIdFrame& /*frame*/) {}

  // Called when a RetireConnectionIdFrame has been parsed.
  virtual void OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& /*frame*/) {}

  // Called when a NewTokenFrame has been parsed.
  virtual void OnNewTokenFrame(const QuicNewTokenFrame& /*frame*/) {}

  // Called when a MessageFrame has been parsed.
  virtual void OnMessageFrame(const QuicMessageFrame& /*frame*/) {}

  // Called when a HandshakeDoneFrame has been parsed.
  virtual void OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& /*frame*/) {}

  // Called when a public reset packet has been received.
  virtual void OnPublicResetPacket(const QuicPublicResetPacket& /*packet*/) {}

  // Called when a version negotiation packet has been received.
  virtual void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& /*packet*/) {}

  // Called when the connection is closed.
  virtual void OnConnectionClosed(const QuicConnectionCloseFrame& /*frame*/,
                                  ConnectionCloseSource /*source*/) {}

  // Called when the version negotiation is successful.
  virtual void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& /*version*/) {}

  // Called when a CachedNetworkParameters is sent to the client.
  virtual void OnSendConnectionState(
      const CachedNetworkParameters& /*cached_network_params*/) {}

  // Called when a CachedNetworkParameters are received from the client.
  virtual void OnReceiveConnectionState(
      const CachedNetworkParameters& /*cached_network_params*/) {}

  // Called when the connection parameters are set from the supplied
  // |config|.
  virtual void OnSetFromConfig(const QuicConfig& /*config*/) {}

  // Called when RTT may have changed, including when an RTT is read from
  // the config.
  virtual void OnRttChanged(QuicTime::Delta /*rtt*/) const {}

  // Called when a StopSendingFrame has been parsed.
  virtual void OnStopSendingFrame(const QuicStopSendingFrame& /*frame*/) {}

  // Called when a PathChallengeFrame has been parsed.
  virtual void OnPathChallengeFrame(const QuicPathChallengeFrame& /*frame*/) {}

  // Called when a PathResponseFrame has been parsed.
  virtual void OnPathResponseFrame(const QuicPathResponseFrame& /*frame*/) {}

  // Called when a StreamsBlockedFrame has been parsed.
  virtual void OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& /*frame*/) {
  }

  // Called when a MaxStreamsFrame has been parsed.
  virtual void OnMaxStreamsFrame(const QuicMaxStreamsFrame& /*frame*/) {}

  // Called when |count| packet numbers have been skipped.
  virtual void OnNPacketNumbersSkipped(QuicPacketCount /*count*/) {}
};

class QUIC_EXPORT_PRIVATE QuicConnectionHelperInterface {
 public:
  virtual ~QuicConnectionHelperInterface() {}

  // Returns a QuicClock to be used for all time related functions.
  virtual const QuicClock* GetClock() const = 0;

  // Returns a QuicRandom to be used for all random number related functions.
  virtual QuicRandom* GetRandomGenerator() = 0;

  // Returns a QuicBufferAllocator to be used for stream send buffers.
  virtual QuicBufferAllocator* GetStreamSendBufferAllocator() = 0;
};

class QUIC_EXPORT_PRIVATE QuicConnection
    : public QuicFramerVisitorInterface,
      public QuicBlockedWriterInterface,
      public QuicPacketCreator::DelegateInterface,
      public QuicSentPacketManager::NetworkChangeVisitor,
      public QuicNetworkBlackholeDetector::Delegate,
      public QuicIdleNetworkDetector::Delegate {
 public:
  // Constructs a new QuicConnection for |connection_id| and
  // |initial_peer_address| using |writer| to write packets. |owns_writer|
  // specifies whether the connection takes ownership of |writer|. |helper| must
  // outlive this connection.
  QuicConnection(QuicConnectionId server_connection_id,
                 QuicSocketAddress initial_peer_address,
                 QuicConnectionHelperInterface* helper,
                 QuicAlarmFactory* alarm_factory,
                 QuicPacketWriter* writer,
                 bool owns_writer,
                 Perspective perspective,
                 const ParsedQuicVersionVector& supported_versions);
  QuicConnection(const QuicConnection&) = delete;
  QuicConnection& operator=(const QuicConnection&) = delete;
  ~QuicConnection() override;

  // Sets connection parameters from the supplied |config|.
  void SetFromConfig(const QuicConfig& config);

  // Called by the session when sending connection state to the client.
  virtual void OnSendConnectionState(
      const CachedNetworkParameters& cached_network_params);

  // Called by the session when receiving connection state from the client.
  virtual void OnReceiveConnectionState(
      const CachedNetworkParameters& cached_network_params);

  // Called by the Session when the client has provided CachedNetworkParameters.
  virtual void ResumeConnectionState(
      const CachedNetworkParameters& cached_network_params,
      bool max_bandwidth_resumption);

  // Called by the Session when a max pacing rate for the connection is needed.
  virtual void SetMaxPacingRate(QuicBandwidth max_pacing_rate);

  // Allows the client to adjust network parameters based on external
  // information.
  void AdjustNetworkParameters(
      const SendAlgorithmInterface::NetworkParams& params);
  void AdjustNetworkParameters(QuicBandwidth bandwidth,
                               QuicTime::Delta rtt,
                               bool allow_cwnd_to_decrease);

  // Install a loss detection tuner. Must be called before OnConfigNegotiated.
  void SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface> tuner);
  // Called by the session when session->is_configured() becomes true.
  void OnConfigNegotiated();

  // Returns the max pacing rate for the connection.
  virtual QuicBandwidth MaxPacingRate() const;

  // Sends crypto handshake messages of length |write_length| to the peer in as
  // few packets as possible. Returns the number of bytes consumed from the
  // data.
  virtual size_t SendCryptoData(EncryptionLevel level,
                                size_t write_length,
                                QuicStreamOffset offset);

  // Send the data of length |write_length| to the peer in as few packets as
  // possible. Returns the number of bytes consumed from data, and a boolean
  // indicating if the fin bit was consumed.  This does not indicate the data
  // has been sent on the wire: it may have been turned into a packet and queued
  // if the socket was unexpectedly blocked.
  virtual QuicConsumedData SendStreamData(QuicStreamId id,
                                          size_t write_length,
                                          QuicStreamOffset offset,
                                          StreamSendingState state);

  // Send |frame| to the peer. Returns true if frame is consumed, false
  // otherwise.
  virtual bool SendControlFrame(const QuicFrame& frame);

  // Called when stream |id| is reset because of |error|.
  virtual void OnStreamReset(QuicStreamId id, QuicRstStreamErrorCode error);

  // Closes the connection.
  // |connection_close_behavior| determines whether or not a connection close
  // packet is sent to the peer.
  virtual void CloseConnection(
      QuicErrorCode error,
      const std::string& details,
      ConnectionCloseBehavior connection_close_behavior);

  // Returns statistics tracked for this connection.
  const QuicConnectionStats& GetStats();

  // Mark stats_.has_non_app_limited_sample as false.
  // TODO(b/151166631) Remove this once the proper fix in b/151166631 is rolled
  // out.
  void ResetHasNonAppLimitedSampleAfterHandshakeCompletion();

  // Processes an incoming UDP packet (consisting of a QuicEncryptedPacket) from
  // the peer.
  // In a client, the packet may be "stray" and have a different connection ID
  // than that of this connection.
  virtual void ProcessUdpPacket(const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address,
                                const QuicReceivedPacket& packet);

  // QuicBlockedWriterInterface
  // Called when the underlying connection becomes writable to allow queued
  // writes to happen.
  void OnBlockedWriterCanWrite() override;

  bool IsWriterBlocked() const override {
    return writer_ != nullptr && writer_->IsWriteBlocked();
  }

  // Called when the caller thinks it's worth a try to write.
  virtual void OnCanWrite();

  // Called when an error occurs while attempting to write a packet to the
  // network.
  void OnWriteError(int error_code);

  // Whether |result| represents a MSG TOO BIG write error.
  bool IsMsgTooBig(const WriteResult& result);

  // If the socket is not blocked, writes queued packets.
  void WriteIfNotBlocked();

  // If the socket is not blocked, writes queued packets and bundles any pending
  // ACKs.
  void WriteAndBundleAcksIfNotBlocked();

  // Set the packet writer.
  void SetQuicPacketWriter(QuicPacketWriter* writer, bool owns_writer) {
    DCHECK(writer != nullptr);
    if (writer_ != nullptr && owns_writer_) {
      delete writer_;
    }
    writer_ = writer;
    owns_writer_ = owns_writer;
  }

  // Set self address.
  void SetSelfAddress(QuicSocketAddress address) { self_address_ = address; }

  // The version of the protocol this connection is using.
  QuicTransportVersion transport_version() const {
    return framer_.transport_version();
  }

  ParsedQuicVersion version() const { return framer_.version(); }

  // The versions of the protocol that this connection supports.
  const ParsedQuicVersionVector& supported_versions() const {
    return framer_.supported_versions();
  }

  // Mark version negotiated for this connection. Once called, the connection
  // will ignore received version negotiation packets.
  void SetVersionNegotiated() {
    version_negotiated_ = true;
    if (perspective_ == Perspective::IS_SERVER) {
      framer_.InferPacketHeaderTypeFromVersion();
    }
  }

  // From QuicFramerVisitorInterface
  void OnError(QuicFramer* framer) override;
  bool OnProtocolVersionMismatch(ParsedQuicVersion received_version) override;
  void OnPacket() override;
  void OnPublicResetPacket(const QuicPublicResetPacket& packet) override;
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) override;
  void OnRetryPacket(QuicConnectionId original_connection_id,
                     QuicConnectionId new_connection_id,
                     quiche::QuicheStringPiece retry_token,
                     quiche::QuicheStringPiece retry_integrity_tag,
                     quiche::QuicheStringPiece retry_without_tag) override;
  bool OnUnauthenticatedPublicHeader(const QuicPacketHeader& header) override;
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override;
  void OnDecryptedPacket(EncryptionLevel level) override;
  bool OnPacketHeader(const QuicPacketHeader& header) override;
  void OnCoalescedPacket(const QuicEncryptedPacket& packet) override;
  void OnUndecryptablePacket(const QuicEncryptedPacket& packet,
                             EncryptionLevel decryption_level,
                             bool has_decryption_key) override;
  bool OnStreamFrame(const QuicStreamFrame& frame) override;
  bool OnCryptoFrame(const QuicCryptoFrame& frame) override;
  bool OnAckFrameStart(QuicPacketNumber largest_acked,
                       QuicTime::Delta ack_delay_time) override;
  bool OnAckRange(QuicPacketNumber start, QuicPacketNumber end) override;
  bool OnAckTimestamp(QuicPacketNumber packet_number,
                      QuicTime timestamp) override;
  bool OnAckFrameEnd(QuicPacketNumber start) override;
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override;
  bool OnPaddingFrame(const QuicPaddingFrame& frame) override;
  bool OnPingFrame(const QuicPingFrame& frame) override;
  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override;
  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override;
  bool OnStopSendingFrame(const QuicStopSendingFrame& frame) override;
  bool OnPathChallengeFrame(const QuicPathChallengeFrame& frame) override;
  bool OnPathResponseFrame(const QuicPathResponseFrame& frame) override;
  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override;
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) override;
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override;
  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override;
  bool OnBlockedFrame(const QuicBlockedFrame& frame) override;
  bool OnNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame) override;
  bool OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame) override;
  bool OnNewTokenFrame(const QuicNewTokenFrame& frame) override;
  bool OnMessageFrame(const QuicMessageFrame& frame) override;
  bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) override;
  void OnPacketComplete() override;
  bool IsValidStatelessResetToken(QuicUint128 token) const override;
  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& packet) override;

  // QuicPacketCreator::DelegateInterface
  bool ShouldGeneratePacket(HasRetransmittableData retransmittable,
                            IsHandshake handshake) override;
  const QuicFrames MaybeBundleAckOpportunistically() override;
  char* GetPacketBuffer() override;
  void OnSerializedPacket(SerializedPacket* packet) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& error_details) override;

  // QuicSentPacketManager::NetworkChangeVisitor
  void OnCongestionChange() override;
  void OnPathMtuIncreased(QuicPacketLength packet_size) override;

  // QuicNetworkBlackholeDetector::Delegate
  void OnPathDegradingDetected() override;
  void OnBlackholeDetected() override;

  // QuicIdleNetworkDetector::Delegate
  void OnHandshakeTimeout() override;
  void OnIdleNetworkDetected() override;

  // Please note, this is not a const function. For logging purpose, please use
  // ack_frame().
  const QuicFrame GetUpdatedAckFrame();

  // Called when the handshake completes. On the client side, handshake
  // completes on receipt of SHLO. On the server side, handshake completes when
  // SHLO gets ACKed (or a forward secure packet gets decrypted successfully).
  // TODO(fayang): Add a guard that this only gets called once.
  void OnHandshakeComplete();

  // Accessors
  void set_visitor(QuicConnectionVisitorInterface* visitor) {
    visitor_ = visitor;
  }
  void set_debug_visitor(QuicConnectionDebugVisitor* debug_visitor) {
    debug_visitor_ = debug_visitor;
    sent_packet_manager_.SetDebugDelegate(debug_visitor);
  }
  // Used in Chromium, but not internally.
  // Must only be called before ping_alarm_ is set.
  void set_ping_timeout(QuicTime::Delta ping_timeout) {
    DCHECK(!ping_alarm_->IsSet());
    ping_timeout_ = ping_timeout;
  }
  const QuicTime::Delta ping_timeout() const { return ping_timeout_; }
  // Used in Chromium, but not internally.
  // Sets an initial timeout for the ping alarm when there is no retransmittable
  // data in flight, allowing for a more aggressive ping alarm in that case.
  void set_initial_retransmittable_on_wire_timeout(
      QuicTime::Delta retransmittable_on_wire_timeout) {
    DCHECK(!ping_alarm_->IsSet());
    initial_retransmittable_on_wire_timeout_ = retransmittable_on_wire_timeout;
  }
  const QuicTime::Delta initial_retransmittable_on_wire_timeout() const {
    return initial_retransmittable_on_wire_timeout_;
  }
  // Used in Chromium, but not internally.
  void set_creator_debug_delegate(QuicPacketCreator::DebugDelegate* visitor) {
    packet_creator_.set_debug_delegate(visitor);
  }
  const QuicSocketAddress& self_address() const { return self_address_; }
  const QuicSocketAddress& peer_address() const { return direct_peer_address_; }
  const QuicSocketAddress& effective_peer_address() const {
    return effective_peer_address_;
  }
  QuicConnectionId connection_id() const { return server_connection_id_; }
  QuicConnectionId client_connection_id() const {
    return client_connection_id_;
  }
  void set_client_connection_id(QuicConnectionId client_connection_id);
  const QuicClock* clock() const { return clock_; }
  QuicRandom* random_generator() const { return random_generator_; }
  QuicByteCount max_packet_length() const;
  void SetMaxPacketLength(QuicByteCount length);

  size_t mtu_probe_count() const { return mtu_probe_count_; }

  bool connected() const { return connected_; }

  // Must only be called on client connections.
  const ParsedQuicVersionVector& server_supported_versions() const {
    DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
    return server_supported_versions_;
  }

  // Testing only.
  size_t NumQueuedPackets() const { return buffered_packets_.size(); }

  // Returns true if the underlying UDP socket is writable, there is
  // no queued data and the connection is not congestion-control
  // blocked.
  bool CanWriteStreamData();

  // Returns true if the connection has queued packets or frames.
  bool HasQueuedData() const;

  // Sets the handshake and idle state connection timeouts.
  void SetNetworkTimeouts(QuicTime::Delta handshake_timeout,
                          QuicTime::Delta idle_timeout);

  // If the connection has timed out, this will close the connection.
  // Otherwise, it will reschedule the timeout alarm.
  void CheckForTimeout();

  // Called when the ping alarm fires. Causes a ping frame to be sent only
  // if the retransmission alarm is not running.
  void OnPingTimeout();

  // Sets up a packet with an QuicAckFrame and sends it out.
  void SendAck();

  // Called when the path degrading alarm fires.
  void OnPathDegradingTimeout();

  // Called when an RTO fires.  Resets the retransmission alarm if there are
  // remaining unacked packets.
  void OnRetransmissionTimeout();

  // Retransmits all unacked packets with retransmittable frames if
  // |retransmission_type| is ALL_UNACKED_PACKETS, otherwise retransmits only
  // initially encrypted packets. Used when the negotiated protocol version is
  // different from what was initially assumed and when the initial encryption
  // changes.
  void RetransmitUnackedPackets(TransmissionType retransmission_type);

  // Calls |sent_packet_manager_|'s NeuterUnencryptedPackets. Used when the
  // connection becomes forward secure and hasn't received acks for all packets.
  void NeuterUnencryptedPackets();

  // Changes the encrypter used for level |level| to |encrypter|.
  void SetEncrypter(EncryptionLevel level,
                    std::unique_ptr<QuicEncrypter> encrypter);

  // Called to remove encrypter of encryption |level|.
  void RemoveEncrypter(EncryptionLevel level);

  // SetNonceForPublicHeader sets the nonce that will be transmitted in the
  // header of each packet encrypted at the initial encryption level decrypted.
  // This should only be called on the server side.
  void SetDiversificationNonce(const DiversificationNonce& nonce);

  // SetDefaultEncryptionLevel sets the encryption level that will be applied
  // to new packets.
  void SetDefaultEncryptionLevel(EncryptionLevel level);

  // SetDecrypter sets the primary decrypter, replacing any that already exists.
  // If an alternative decrypter is in place then the function DCHECKs. This is
  // intended for cases where one knows that future packets will be using the
  // new decrypter and the previous decrypter is now obsolete. |level| indicates
  // the encryption level of the new decrypter.
  void SetDecrypter(EncryptionLevel level,
                    std::unique_ptr<QuicDecrypter> decrypter);

  // SetAlternativeDecrypter sets a decrypter that may be used to decrypt
  // future packets. |level| indicates the encryption level of the decrypter. If
  // |latch_once_used| is true, then the first time that the decrypter is
  // successful it will replace the primary decrypter.  Otherwise both
  // decrypters will remain active and the primary decrypter will be the one
  // last used.
  void SetAlternativeDecrypter(EncryptionLevel level,
                               std::unique_ptr<QuicDecrypter> decrypter,
                               bool latch_once_used);

  void InstallDecrypter(EncryptionLevel level,
                        std::unique_ptr<QuicDecrypter> decrypter);
  void RemoveDecrypter(EncryptionLevel level);

  const QuicDecrypter* decrypter() const;
  const QuicDecrypter* alternative_decrypter() const;

  Perspective perspective() const { return perspective_; }

  // Allow easy overriding of truncated connection IDs.
  void set_can_truncate_connection_ids(bool can) {
    can_truncate_connection_ids_ = can;
  }

  // Returns the underlying sent packet manager.
  const QuicSentPacketManager& sent_packet_manager() const {
    return sent_packet_manager_;
  }

  // Returns the underlying sent packet manager.
  QuicSentPacketManager& sent_packet_manager() { return sent_packet_manager_; }

  UberReceivedPacketManager& received_packet_manager() {
    return uber_received_packet_manager_;
  }

  bool CanWrite(HasRetransmittableData retransmittable);

  // When the flusher is out of scope, only the outermost flusher will cause a
  // flush of the connection and set the retransmission alarm if there is one
  // pending.  In addition, this flusher can be configured to ensure that an ACK
  // frame is included in the first packet created, if there's new ack
  // information to be sent.
  class QUIC_EXPORT_PRIVATE ScopedPacketFlusher {
   public:
    explicit ScopedPacketFlusher(QuicConnection* connection);
    ~ScopedPacketFlusher();

   private:
    QuicConnection* connection_;
    // If true, when this flusher goes out of scope, flush connection and set
    // retransmission alarm if there is one pending.
    bool flush_and_set_pending_retransmission_alarm_on_delete_;
  };

  QuicPacketWriter* writer() { return writer_; }
  const QuicPacketWriter* writer() const { return writer_; }

  // Sends an MTU discovery packet of size |target_mtu|.  If the packet is
  // acknowledged by the peer, the maximum packet size will be increased to
  // |target_mtu|.
  void SendMtuDiscoveryPacket(QuicByteCount target_mtu);

  // Sends a connectivity probing packet to |peer_address| with
  // |probing_writer|. If |probing_writer| is nullptr, will use default
  // packet writer to write the packet. Returns true if subsequent packets can
  // be written to the probing writer. If connection is V99, a padded IETF QUIC
  // PATH_CHALLENGE packet is transmitted; if not V99, a Google QUIC padded PING
  // packet is transmitted.
  virtual bool SendConnectivityProbingPacket(
      QuicPacketWriter* probing_writer,
      const QuicSocketAddress& peer_address);

  // Sends response to a connectivity probe. Sends either a Padded Ping
  // or an IETF PATH_RESPONSE based on the version of the connection.
  // Is the counterpart to SendConnectivityProbingPacket().
  virtual void SendConnectivityProbingResponsePacket(
      const QuicSocketAddress& peer_address);

  // Sends an MTU discovery packet and updates the MTU discovery alarm.
  void DiscoverMtu();

  // Sets the session notifier on the SentPacketManager.
  void SetSessionNotifier(SessionNotifierInterface* session_notifier);

  // Set data producer in framer.
  void SetDataProducer(QuicStreamFrameDataProducer* data_producer);

  // Set transmission type of next sending packets.
  void SetTransmissionType(TransmissionType type);

  // Tries to send |message| and returns the message status.
  // If |flush| is false, this will return a MESSAGE_STATUS_BLOCKED
  // when the connection is deemed unwritable.
  virtual MessageStatus SendMessage(QuicMessageId message_id,
                                    QuicMemSliceSpan message,
                                    bool flush);

  // Returns the largest payload that will fit into a single MESSAGE frame.
  // Because overhead can vary during a connection, this method should be
  // checked for every message.
  QuicPacketLength GetCurrentLargestMessagePayload() const;
  // Returns the largest payload that will fit into a single MESSAGE frame at
  // any point during the connection.  This assumes the version and
  // connection ID lengths do not change.
  QuicPacketLength GetGuaranteedLargestMessagePayload() const;

  // Returns the id of the cipher last used for decrypting packets.
  uint32_t cipher_id() const;

  std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets() {
    return termination_packets_.get();
  }

  bool ack_frame_updated() const;

  QuicConnectionHelperInterface* helper() { return helper_; }
  QuicAlarmFactory* alarm_factory() { return alarm_factory_; }

  quiche::QuicheStringPiece GetCurrentPacket();

  const QuicFramer& framer() const { return framer_; }

  const QuicPacketCreator& packet_creator() const { return packet_creator_; }

  EncryptionLevel encryption_level() const { return encryption_level_; }
  EncryptionLevel last_decrypted_level() const {
    return last_decrypted_packet_level_;
  }

  const QuicSocketAddress& last_packet_source_address() const {
    return last_packet_source_address_;
  }

  bool fill_up_link_during_probing() const {
    return fill_up_link_during_probing_;
  }
  void set_fill_up_link_during_probing(bool new_value) {
    fill_up_link_during_probing_ = new_value;
  }

  // This setting may be changed during the crypto handshake in order to
  // enable/disable padding of different packets in the crypto handshake.
  //
  // This setting should never be set to false in public facing endpoints. It
  // can only be set to false if there is some other mechanism of preventing
  // amplification attacks, such as ICE (plus its a non-standard quic).
  void set_fully_pad_crypto_handshake_packets(bool new_value) {
    packet_creator_.set_fully_pad_crypto_handshake_packets(new_value);
  }

  bool fully_pad_during_crypto_handshake() const {
    return packet_creator_.fully_pad_crypto_handshake_packets();
  }

  size_t min_received_before_ack_decimation() const;
  void set_min_received_before_ack_decimation(size_t new_value);

  size_t ack_frequency_before_ack_decimation() const;
  void set_ack_frequency_before_ack_decimation(size_t new_value);

  // If |defer| is true, configures the connection to defer sending packets in
  // response to an ACK to the SendAlarm. If |defer| is false, packets may be
  // sent immediately after receiving an ACK.
  void set_defer_send_in_response_to_packets(bool defer) {
    defer_send_in_response_to_packets_ = defer;
  }

  // Sets the current per-packet options for the connection. The QuicConnection
  // does not take ownership of |options|; |options| must live for as long as
  // the QuicConnection is in use.
  void set_per_packet_options(PerPacketOptions* options) {
    per_packet_options_ = options;
  }

  bool IsPathDegrading() const { return is_path_degrading_; }

  // Attempts to process any queued undecryptable packets.
  void MaybeProcessUndecryptablePackets();

  // Queue a coalesced packet.
  void QueueCoalescedPacket(const QuicEncryptedPacket& packet);

  // Process previously queued coalesced packets.
  void MaybeProcessCoalescedPackets();

  enum PacketContent : uint8_t {
    NO_FRAMES_RECEIVED,
    // TODO(fkastenholz): Change name when we get rid of padded ping/
    // pre-version-99.
    // Also PATH CHALLENGE and PATH RESPONSE.
    FIRST_FRAME_IS_PING,
    SECOND_FRAME_IS_PADDING,
    NOT_PADDED_PING,  // Set if the packet is not {PING, PADDING}.
  };

  // Whether the handshake completes from this connection's perspective.
  bool IsHandshakeComplete() const;

  // Whether peer completes handshake. Only used with TLS handshake.
  bool IsHandshakeConfirmed() const;

  // Returns the largest received packet number sent by peer.
  QuicPacketNumber GetLargestReceivedPacket() const;

  // Adds the connection ID to a set of connection IDs that are accepted as
  // destination on incoming packets.
  void AddIncomingConnectionId(QuicConnectionId connection_id);

  // Called when ACK alarm goes off. Sends ACKs of those packet number spaces
  // which have expired ACK timeout. Only used when this connection supports
  // multiple packet number spaces.
  void SendAllPendingAcks();

  // Returns true if this connection supports multiple packet number spaces.
  bool SupportsMultiplePacketNumberSpaces() const;

  // For logging purpose.
  const QuicAckFrame& ack_frame() const;

  // Install encrypter and decrypter for ENCRYPTION_INITIAL using
  // |connection_id| as the first client-sent destination connection ID,
  // or the one sent after an IETF Retry.
  void InstallInitialCrypters(QuicConnectionId connection_id);

  // Called when version is considered negotiated.
  void OnSuccessfulVersionNegotiation();

 protected:
  // Calls cancel() on all the alarms owned by this connection.
  void CancelAllAlarms();

  // Send a packet to the peer, and takes ownership of the packet if the packet
  // cannot be written immediately.
  virtual void SendOrQueuePacket(SerializedPacket* packet);

  // Called after a packet is received from a new effective peer address and is
  // decrypted. Starts validation of effective peer's address change. Calls
  // OnConnectionMigration as soon as the address changed.
  void StartEffectivePeerMigration(AddressChangeType type);

  // Called when a effective peer address migration is validated.
  virtual void OnEffectivePeerMigrationValidated();

  // Get the effective peer address from the packet being processed. For proxied
  // connections, effective peer address is the address of the endpoint behind
  // the proxy. For non-proxied connections, effective peer address is the same
  // as peer address.
  //
  // Notes for implementations in subclasses:
  // - If the connection is not proxied, the overridden method should use the
  //   base implementation:
  //
  //       return QuicConnection::GetEffectivePeerAddressFromCurrentPacket();
  //
  // - If the connection is proxied, the overridden method may return either of
  //   the following:
  //   a) The address of the endpoint behind the proxy. The address is used to
  //      drive effective peer migration.
  //   b) An uninitialized address, meaning the effective peer address does not
  //      change.
  virtual QuicSocketAddress GetEffectivePeerAddressFromCurrentPacket() const;

  // Selects and updates the version of the protocol being used by selecting a
  // version from |available_versions| which is also supported. Returns true if
  // such a version exists, false otherwise.
  bool SelectMutualVersion(const ParsedQuicVersionVector& available_versions);

  // Returns the current per-packet options for the connection.
  PerPacketOptions* per_packet_options() { return per_packet_options_; }

  AddressChangeType active_effective_peer_migration_type() const {
    return active_effective_peer_migration_type_;
  }

  // Sends a connection close packet to the peer and includes an ACK if the ACK
  // is not empty, the |error| is not PACKET_WRITE_ERROR, and it fits.
  virtual void SendConnectionClosePacket(QuicErrorCode error,
                                         const std::string& details);

  // Returns true if the packet should be discarded and not sent.
  virtual bool ShouldDiscardPacket(const SerializedPacket& packet);

  // Retransmits packets continuously until blocked by the congestion control.
  // If there are no packets to retransmit, does not do anything.
  void SendProbingRetransmissions();

  // Decides whether to send probing retransmissions, and does so if required.
  void MaybeSendProbingRetransmissions();

  // Notify various components(SendPacketManager, Session etc.) that this
  // connection has been migrated.
  virtual void OnConnectionMigration(AddressChangeType addr_change_type);

  // Return whether the packet being processed is a connectivity probing.
  // A packet is a connectivity probing if it is a padded ping packet with self
  // and/or peer address changes.
  bool IsCurrentPacketConnectivityProbing() const;

  // Return true iff the writer is blocked, if blocked, call
  // visitor_->OnWriteBlocked() to add the connection into the write blocked
  // list.
  bool HandleWriteBlocked();

 private:
  friend class test::QuicConnectionPeer;

  typedef std::list<SerializedPacket> QueuedPacketList;

  // BufferedPacket stores necessary information (encrypted buffer and self/peer
  // addresses) of those packets which are serialized but failed to send because
  // socket is blocked. From unacked packet map and send algorithm's
  // perspective, buffered packets are treated as sent.
  struct QUIC_EXPORT_PRIVATE BufferedPacket {
    BufferedPacket(const SerializedPacket& packet,
                   const QuicSocketAddress& self_address,
                   const QuicSocketAddress& peer_address);
    BufferedPacket(char* encrypted_buffer,
                   QuicPacketLength encrypted_length,
                   const QuicSocketAddress& self_address,
                   const QuicSocketAddress& peer_address);
    BufferedPacket(const BufferedPacket& other) = delete;
    BufferedPacket(const BufferedPacket&& other) = delete;

    ~BufferedPacket();

    // encrypted_buffer is owned by buffered packet.
    quiche::QuicheStringPiece encrypted_buffer;
    // Self and peer addresses when the packet is serialized.
    const QuicSocketAddress self_address;
    const QuicSocketAddress peer_address;
  };

  // Notifies the visitor of the close and marks the connection as disconnected.
  // Does not send a connection close frame to the peer. It should only be
  // called by CloseConnection or OnConnectionCloseFrame, OnPublicResetPacket,
  // and OnAuthenticatedIetfStatelessResetPacket.
  void TearDownLocalConnectionState(QuicErrorCode error,
                                    const std::string& details,
                                    ConnectionCloseSource source);
  void TearDownLocalConnectionState(const QuicConnectionCloseFrame& frame,
                                    ConnectionCloseSource source);

  // Writes the given packet to socket, encrypted with packet's
  // encryption_level. Returns true on successful write, and false if the writer
  // was blocked and the write needs to be tried again. Notifies the
  // SentPacketManager when the write is successful and sets
  // retransmittable frames to nullptr.
  // Saves the connection close packet for later transmission, even if the
  // writer is write blocked.
  bool WritePacket(SerializedPacket* packet);

  // Flush packets buffered in the writer, if any.
  void FlushPackets();

  // Make sure a stop waiting we got from our peer is sane.
  // Returns nullptr if the frame is valid or an error string if it was invalid.
  const char* ValidateStopWaitingFrame(
      const QuicStopWaitingFrame& stop_waiting);

  // Sends a version negotiation packet to the peer.
  void SendVersionNegotiationPacket(bool ietf_quic, bool has_length_prefix);

  // Clears any accumulated frames from the last received packet.
  void ClearLastFrames();

  // Deletes and clears any queued packets.
  void ClearQueuedPackets();

  // Closes the connection if the sent packet manager is tracking too many
  // outstanding packets.
  void CloseIfTooManyOutstandingSentPackets();

  // Writes as many queued packets as possible.  The connection must not be
  // blocked when this is called.
  void WriteQueuedPackets();

  // Writes new data if congestion control allows.
  void WriteNewData();

  // Queues |packet| in the hopes that it can be decrypted in the
  // future, when a new key is installed.
  void QueueUndecryptablePacket(const QuicEncryptedPacket& packet);

  // Sends any packets which are a response to the last packet, including both
  // acks and pending writes if an ack opened the congestion window.
  void MaybeSendInResponseToPacket();

  // Gets the least unacked packet number, which is the next packet number to be
  // sent if there are no outstanding packets.
  QuicPacketNumber GetLeastUnacked() const;

  // Sets the timeout alarm to the appropriate value, if any.
  void SetTimeoutAlarm();

  // Sets the ping alarm to the appropriate value, if any.
  void SetPingAlarm();

  // Sets the retransmission alarm based on SentPacketManager.
  void SetRetransmissionAlarm();

  // Sets the path degrading alarm.
  void SetPathDegradingAlarm();

  // Sets the MTU discovery alarm if necessary.
  // |sent_packet_number| is the recently sent packet number.
  void MaybeSetMtuAlarm(QuicPacketNumber sent_packet_number);

  HasRetransmittableData IsRetransmittable(const SerializedPacket& packet);
  bool IsTerminationPacket(const SerializedPacket& packet);

  // Set the size of the packet we are targeting while doing path MTU discovery.
  void SetMtuDiscoveryTarget(QuicByteCount target);

  // Returns |suggested_max_packet_size| clamped to any limits set by the
  // underlying writer, connection, or protocol.
  QuicByteCount GetLimitedMaxPacketSize(
      QuicByteCount suggested_max_packet_size);

  // Do any work which logically would be done in OnPacket but can not be
  // safely done until the packet is validated. Returns true if packet can be
  // handled, false otherwise.
  bool ProcessValidatedPacket(const QuicPacketHeader& header);

  // Returns true if received |packet_number| can be processed. Please note,
  // this is called after packet got decrypted successfully.
  bool ValidateReceivedPacketNumber(QuicPacketNumber packet_number);

  // Consider receiving crypto frame on non crypto stream as memory corruption.
  bool MaybeConsiderAsMemoryCorruption(const QuicStreamFrame& frame);

  // Check if the connection has no outstanding data to send and notify
  // congestion controller if it is the case.
  void CheckIfApplicationLimited();

  // Sets |current_packet_content_| to |type| if applicable. And
  // starts effective peer migration if current packet is confirmed not a
  // connectivity probe and |current_effective_peer_migration_type_| indicates
  // effective peer address change.
  void UpdatePacketContent(PacketContent type);

  // Called when last received ack frame has been processed.
  // |send_stop_waiting| indicates whether a stop waiting needs to be sent.
  // |acked_new_packet| is true if a previously-unacked packet was acked.
  void PostProcessAfterAckFrame(bool send_stop_waiting, bool acked_new_packet);

  // Called when an ACK is received to set the path degrading alarm or
  // retransmittable on wire alarm.
  void MaybeSetPathDegradingAlarm(bool acked_new_packet);

  // Updates the release time into the future.
  void UpdateReleaseTimeIntoFuture();

  // Sends generic path probe packet to the peer. If we are not IETF QUIC, will
  // always send a padded ping, regardless of whether this is a request or
  // response. If version 99/ietf quic, will send a PATH_RESPONSE if
  // |is_response| is true, a PATH_CHALLENGE if not.
  bool SendGenericPathProbePacket(QuicPacketWriter* probing_writer,
                                  const QuicSocketAddress& peer_address,
                                  bool is_response);

  // Called when an ACK is about to send. Resets ACK related internal states,
  // e.g., cancels ack_alarm_, resets
  // num_retransmittable_packets_received_since_last_ack_sent_ etc.
  void ResetAckStates();

  // Returns true if the ACK frame should be bundled with ACK-eliciting frame.
  bool ShouldBundleRetransmittableFrameWithAck() const;

  void PopulateStopWaitingFrame(QuicStopWaitingFrame* stop_waiting);

  // Enables multiple packet number spaces support based on handshake protocol
  // and flags.
  void MaybeEnableMultiplePacketNumberSpacesSupport();

  // Returns packet fate when trying to write a packet via WritePacket().
  SerializedPacketFate DeterminePacketFate(bool is_mtu_discovery);

  // Serialize and send coalesced_packet. Returns false if serialization fails
  // or the write causes errors, otherwise, returns true.
  bool FlushCoalescedPacket();

  // Returns the encryption level the connection close packet should be sent at,
  // which is the highest encryption level that peer can guarantee to process.
  EncryptionLevel GetConnectionCloseEncryptionLevel() const;

  // Called after an ACK frame is successfully processed to update largest
  // received packet number which contains an ACK frame.
  void SetLargestReceivedPacketWithAck(QuicPacketNumber new_value);

  // Returns largest received packet number which contains an ACK frame.
  QuicPacketNumber GetLargestReceivedPacketWithAck() const;

  // Returns the largest packet number that has been sent.
  QuicPacketNumber GetLargestSentPacket() const;

  // Returns the largest sent packet number that has been ACKed by peer.
  QuicPacketNumber GetLargestAckedPacket() const;

  // Whether incoming_connection_ids_ contains connection_id.
  bool HasIncomingConnectionId(QuicConnectionId connection_id);

  // Whether connection enforces anti-amplification limit.
  bool EnforceAntiAmplificationLimit() const;

  // Whether connection is limited by amplification factor.
  bool LimitedByAmplificationFactor() const;

  // We've got a packet write error, should we ignore it?
  // NOTE: This is not a const function - if return true, the max packet size is
  // reverted to a previous(smaller) value to avoid write errors in the future.
  bool ShouldIgnoreWriteError();

  // Returns path degrading deadline. QuicTime::Zero() means no path degrading
  // detection is needed.
  QuicTime GetPathDegradingDeadline() const;

  // Returns true if path degrading should be detected.
  bool ShouldDetectPathDegrading() const;

  // Returns network blackhole deadline. QuicTime::Zero() means no blackhole
  // detection is needed.
  QuicTime GetNetworkBlackholeDeadline() const;

  // Returns true if network blackhole should be detected.
  bool ShouldDetectBlackhole() const;

  // Remove these two when deprecating quic_use_idle_network_detector.
  QuicTime::Delta GetHandshakeTimeout() const;
  QuicTime GetTimeOfLastReceivedPacket() const;

  QuicFramer framer_;

  // Contents received in the current packet, especially used to identify
  // whether the current packet is a padded PING packet.
  PacketContent current_packet_content_;
  // Set to true as soon as the packet currently being processed has been
  // detected as a connectivity probing.
  // Always false outside the context of ProcessUdpPacket().
  bool is_current_packet_connectivity_probing_;

  // Caches the current effective peer migration type if a effective peer
  // migration might be initiated. As soon as the current packet is confirmed
  // not a connectivity probe, effective peer migration will start.
  AddressChangeType current_effective_peer_migration_type_;
  QuicConnectionHelperInterface* helper_;  // Not owned.
  QuicAlarmFactory* alarm_factory_;        // Not owned.
  PerPacketOptions* per_packet_options_;   // Not owned.
  QuicPacketWriter* writer_;  // Owned or not depending on |owns_writer_|.
  bool owns_writer_;
  // Encryption level for new packets. Should only be changed via
  // SetDefaultEncryptionLevel().
  EncryptionLevel encryption_level_;
  const QuicClock* clock_;
  QuicRandom* random_generator_;

  QuicConnectionId server_connection_id_;
  QuicConnectionId client_connection_id_;
  // On the server, the connection ID is set when receiving the first packet.
  // This variable ensures we only set it this way once.
  bool client_connection_id_is_set_;
  // Address on the last successfully processed packet received from the
  // direct peer.
  QuicSocketAddress self_address_;
  QuicSocketAddress peer_address_;

  QuicSocketAddress direct_peer_address_;
  // Address of the endpoint behind the proxy if the connection is proxied.
  // Otherwise it is the same as |peer_address_|.
  // NOTE: Currently |effective_peer_address_| and |peer_address_| are always
  // the same(the address of the direct peer), but soon we'll change
  // |effective_peer_address_| to be the address of the endpoint behind the
  // proxy if the connection is proxied.
  QuicSocketAddress effective_peer_address_;

  // Records change type when the effective peer initiates migration to a new
  // address. Reset to NO_CHANGE after effective peer migration is validated.
  AddressChangeType active_effective_peer_migration_type_;

  // Records highest sent packet number when effective peer migration is
  // started.
  QuicPacketNumber highest_packet_sent_before_effective_peer_migration_;

  // True if the last packet has gotten far enough in the framer to be
  // decrypted.
  bool last_packet_decrypted_;
  QuicByteCount last_size_;  // Size of the last received packet.
  // TODO(rch): remove this when b/27221014 is fixed.
  const char* current_packet_data_;  // UDP payload of packet currently being
                                     // parsed or nullptr.
  EncryptionLevel last_decrypted_packet_level_;
  QuicPacketHeader last_header_;
  bool should_last_packet_instigate_acks_;

  // Track some peer state so we can do less bookkeeping
  // Largest sequence sent by the peer which had an ack frame (latest ack info).
  // Do not read or write directly, use GetLargestReceivedPacketWithAck() and
  // SetLargestReceivedPacketWithAck() instead.
  QuicPacketNumber largest_seen_packet_with_ack_;
  // Largest packet number sent by the peer which had an ACK frame per packet
  // number space. Only used when this connection supports multiple packet
  // number spaces.
  QuicPacketNumber largest_seen_packets_with_ack_[NUM_PACKET_NUMBER_SPACES];

  // Largest packet number sent by the peer which had a stop waiting frame.
  QuicPacketNumber largest_seen_packet_with_stop_waiting_;

  // Collection of packets which were received before encryption was
  // established, but which could not be decrypted.  We buffer these on
  // the assumption that they could not be processed because they were
  // sent with the INITIAL encryption and the CHLO message was lost.
  QuicCircularDeque<std::unique_ptr<QuicEncryptedPacket>>
      undecryptable_packets_;

  // Collection of coalesced packets which were received while processing
  // the current packet.
  QuicCircularDeque<std::unique_ptr<QuicEncryptedPacket>>
      received_coalesced_packets_;

  // Maximum number of undecryptable packets the connection will store.
  size_t max_undecryptable_packets_;

  // Maximum number of tracked packets.
  QuicPacketCount max_tracked_packets_;

  // When the version negotiation packet could not be sent because the socket
  // was not writable, this is set to true.
  bool pending_version_negotiation_packet_;
  // Used when pending_version_negotiation_packet_ is true.
  bool send_ietf_version_negotiation_packet_;
  bool send_version_negotiation_packet_with_prefixed_lengths_;

  // Contains the connection close packets if the connection has been closed.
  std::unique_ptr<std::vector<std::unique_ptr<QuicEncryptedPacket>>>
      termination_packets_;

  // Determines whether or not a connection close packet is sent to the peer
  // after idle timeout due to lack of network activity.
  // This is particularly important on mobile, where waking up the radio is
  // undesirable.
  ConnectionCloseBehavior idle_timeout_connection_close_behavior_;

  // When true, close the QUIC connection after 5 RTOs.  Due to the min rto of
  // 200ms, this is over 5 seconds.
  bool close_connection_after_five_rtos_;

  UberReceivedPacketManager uber_received_packet_manager_;

  // Indicates how many consecutive times an ack has arrived which indicates
  // the peer needs to stop waiting for some packets.
  // TODO(fayang): remove this when deprecating QUIC_VERSION_43.
  int stop_waiting_count_;

  // Indicates the retransmission alarm needs to be set.
  bool pending_retransmission_alarm_;

  // If true, defer sending data in response to received packets to the
  // SendAlarm.
  bool defer_send_in_response_to_packets_;

  // The timeout for PING.
  QuicTime::Delta ping_timeout_;

  // Initial timeout for how long the wire can have no retransmittable packets.
  QuicTime::Delta initial_retransmittable_on_wire_timeout_;

  // Indicates how many retransmittable-on-wire pings have been emitted without
  // receiving any new data in between.
  int consecutive_retransmittable_on_wire_ping_count_;

  // Arena to store class implementations within the QuicConnection.
  QuicConnectionArena arena_;

  // An alarm that fires when an ACK should be sent to the peer.
  QuicArenaScopedPtr<QuicAlarm> ack_alarm_;
  // An alarm that fires when a packet needs to be retransmitted.
  QuicArenaScopedPtr<QuicAlarm> retransmission_alarm_;
  // An alarm that is scheduled when the SentPacketManager requires a delay
  // before sending packets and fires when the packet may be sent.
  QuicArenaScopedPtr<QuicAlarm> send_alarm_;
  // An alarm that is scheduled when the connection can still write and there
  // may be more data to send.
  // An alarm that fires when the connection may have timed out.
  // TODO(fayang): Remove this when deprecating quic_use_idle_network_detector.
  QuicArenaScopedPtr<QuicAlarm> timeout_alarm_;
  // An alarm that fires when a ping should be sent.
  QuicArenaScopedPtr<QuicAlarm> ping_alarm_;
  // An alarm that fires when an MTU probe should be sent.
  QuicArenaScopedPtr<QuicAlarm> mtu_discovery_alarm_;
  // An alarm that fires when this connection is considered degrading.
  // TODO(fayang): Remove this when deprecating quic_use_blackhole_detector
  // flag.
  QuicArenaScopedPtr<QuicAlarm> path_degrading_alarm_;
  // An alarm that fires to process undecryptable packets when new decyrption
  // keys are available.
  QuicArenaScopedPtr<QuicAlarm> process_undecryptable_packets_alarm_;
  // Neither visitor is owned by this class.
  QuicConnectionVisitorInterface* visitor_;
  QuicConnectionDebugVisitor* debug_visitor_;

  QuicPacketCreator packet_creator_;

  // TODO(fayang): Remove these two when deprecating
  // quic_use_idle_network_detector.
  // Network idle time before this connection is closed.
  QuicTime::Delta idle_network_timeout_;
  // The connection will wait this long for the handshake to complete.
  QuicTime::Delta handshake_timeout_;

  // Statistics for this session.
  QuicConnectionStats stats_;

  // Timestamps used for timeouts.
  // The time of the first retransmittable packet that was sent after the most
  // recently received packet.
  // TODO(fayang): Remove these two when deprecating
  // quic_use_idle_network_detector.
  QuicTime time_of_first_packet_sent_after_receiving_;
  // The time that a packet is received for this connection. Initialized to
  // connection creation time.
  // This is used for timeouts, and does not indicate the packet was processed.
  QuicTime time_of_last_received_packet_;

  // Sent packet manager which tracks the status of packets sent by this
  // connection and contains the send and receive algorithms to determine when
  // to send packets.
  QuicSentPacketManager sent_packet_manager_;

  // Indicates whether connection version has been negotiated.
  // Always true for server connections.
  bool version_negotiated_;

  // Tracks if the connection was created by the server or the client.
  Perspective perspective_;

  // True by default.  False if we've received or sent an explicit connection
  // close.
  bool connected_;

  // Destination address of the last received packet.
  QuicSocketAddress last_packet_destination_address_;

  // Source address of the last received packet.
  QuicSocketAddress last_packet_source_address_;

  // Set to false if the connection should not send truncated connection IDs to
  // the peer, even if the peer supports it.
  bool can_truncate_connection_ids_;

  // If non-empty this contains the set of versions received in a
  // version negotiation packet.
  ParsedQuicVersionVector server_supported_versions_;

  // The number of MTU probes already sent.
  size_t mtu_probe_count_;

  // The value of |long_term_mtu_| prior to the last successful MTU increase.
  // 0 means either
  // - MTU discovery has never been enabled, or
  // - MTU discovery has been enabled, but the connection got a packet write
  //   error with a new (successfully probed) MTU, so it reverted
  //   |long_term_mtu_| to the value before the last increase.
  QuicPacketLength previous_validated_mtu_;
  // The value of the MTU regularly used by the connection. This is different
  // from the value returned by max_packet_size(), as max_packet_size() returns
  // the value of the MTU as currently used by the serializer, so if
  // serialization of an MTU probe is in progress, those two values will be
  // different.
  QuicByteCount long_term_mtu_;

  // The maximum UDP payload size that our peer has advertised support for.
  // Defaults to kDefaultMaxPacketSizeTransportParam until received from peer.
  QuicByteCount peer_max_packet_size_;

  // The size of the largest packet received from peer.
  QuicByteCount largest_received_packet_size_;

  // Indicates whether a write error is encountered currently. This is used to
  // avoid infinite write errors.
  bool write_error_occurred_;

  // Indicates not to send or process stop waiting frames.
  bool no_stop_waiting_frames_;

  // Consecutive number of sent packets which have no retransmittable frames.
  size_t consecutive_num_packets_with_no_retransmittable_frames_;

  // After this many packets sent without retransmittable frames, an artificial
  // retransmittable frame(a WINDOW_UPDATE) will be created to solicit an ack
  // from the peer. Default to kMaxConsecutiveNonRetransmittablePackets.
  size_t max_consecutive_num_packets_with_no_retransmittable_frames_;

  // If true, bundle an ack-eliciting frame with an ACK if the PTO or RTO alarm
  // have previously fired.
  bool bundle_retransmittable_with_pto_ack_;

  // If true, the connection will fill up the pipe with extra data whenever the
  // congestion controller needs it in order to make a bandwidth estimate.  This
  // is useful if the application pesistently underutilizes the link, but still
  // relies on having a reasonable bandwidth estimate from the connection, e.g.
  // for real time applications.
  bool fill_up_link_during_probing_;

  // If true, the probing retransmission will not be started again.  This is
  // used to safeguard against an accidental tail recursion in probing
  // retransmission code.
  bool probing_retransmission_pending_;

  // Indicates whether a stateless reset token has been received from peer.
  bool stateless_reset_token_received_;
  // Stores received stateless reset token from peer. Used to verify whether a
  // packet is a stateless reset packet.
  QuicUint128 received_stateless_reset_token_;

  // Id of latest sent control frame. 0 if no control frame has been sent.
  QuicControlFrameId last_control_frame_id_;

  // True if the peer is unreachable on the current path.
  bool is_path_degrading_;

  // True if an ack frame is being processed.
  bool processing_ack_frame_;

  // True if the writer supports release timestamp.
  bool supports_release_time_;

  // Time this connection can release packets into the future.
  QuicTime::Delta release_time_into_future_;

  // Payload of most recently transmitted IETF QUIC connectivity
  // probe packet (the PATH_CHALLENGE payload). This implementation transmits
  // only one PATH_CHALLENGE per connectivity probe, so only one
  // QuicPathFrameBuffer is needed.
  std::unique_ptr<QuicPathFrameBuffer> transmitted_connectivity_probe_payload_;

  // Payloads that were received in the most recent probe. This needs to be a
  // Deque because the peer might no be using this implementation, and others
  // might send a packet with more than one PATH_CHALLENGE, so all need to be
  // saved and responded to.
  QuicCircularDeque<QuicPathFrameBuffer> received_path_challenge_payloads_;

  // Set of connection IDs that should be accepted as destination on
  // received packets. This is conceptually a set but is implemented as a
  // vector to improve performance since it is expected to be very small.
  std::vector<QuicConnectionId> incoming_connection_ids_;

  // Indicates whether received RETRY packets should be dropped.
  bool drop_incoming_retry_packets_;

  // If max_consecutive_ptos_ > 0, close connection if consecutive PTOs is
  // greater than max_consecutive_ptos.
  size_t max_consecutive_ptos_;

  // Bytes received before address validation. Only used when
  // EnforceAntiAmplificationLimit returns true.
  size_t bytes_received_before_address_validation_;

  // Bytes sent before address validation. Only used when
  // EnforceAntiAmplificationLimit returns true.
  size_t bytes_sent_before_address_validation_;

  // True if peer address has been validated. Address is considered validated
  // when 1) an address token is received and validated, or 2) a HANDSHAKE
  // packet has been successfully processed. Only used when
  // EnforceAntiAmplificationLimit returns true.
  bool address_validated_;

  // Used to store content of packets which cannot be sent because of write
  // blocked. Packets' encrypted buffers are copied and owned by
  // buffered_packets_. From unacked_packet_map (and congestion control)'s
  // perspective, those packets are considered sent.
  std::list<BufferedPacket> buffered_packets_;

  // Used to coalesce packets of different encryption level into the same UDP
  // datagram. Connection stops trying to coalesce packets if a forward secure
  // packet gets acknowledged.
  QuicCoalescedPacket coalesced_packet_;

  QuicConnectionMtuDiscoverer mtu_discoverer_;

  QuicNetworkBlackholeDetector blackhole_detector_;

  QuicIdleNetworkDetector idle_network_detector_;

  const bool use_blackhole_detector_ =
      GetQuicReloadableFlag(quic_use_blackhole_detector);

  const bool use_idle_network_detector_ =
      use_blackhole_detector_ &&
      GetQuicReloadableFlag(quic_use_idle_network_detector);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_H_
