// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
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

#ifndef NET_QUIC_CORE_QUIC_CONNECTION_H_
#define NET_QUIC_CORE_QUIC_CONNECTION_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/quic_alarm.h"
#include "net/quic/core/quic_alarm_factory.h"
#include "net/quic/core/quic_blocked_writer_interface.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/core/quic_framer.h"
#include "net/quic/core/quic_one_block_arena.h"
#include "net/quic/core/quic_packet_creator.h"
#include "net/quic/core/quic_packet_generator.h"
#include "net/quic/core/quic_packet_writer.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_received_packet_manager.h"
#include "net/quic/core/quic_sent_packet_manager.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QuicClock;
class QuicConfig;
class QuicConnection;
class QuicDecrypter;
class QuicEncrypter;
class QuicRandom;

namespace test {
class PacketSavingConnection;
class QuicConnectionPeer;
}  // namespace test

// The initial number of packets between MTU probes.  After each attempt the
// number is doubled.
const QuicPacketCount kPacketsBetweenMtuProbesBase = 100;

// The number of MTU probes that get sent before giving up.
const size_t kMtuDiscoveryAttempts = 3;

// Ensure that exponential back-off does not result in an integer overflow.
// The number of packets can be potentially capped, but that is not useful at
// current kMtuDiscoveryAttempts value, and hence is not implemented at present.
static_assert(kMtuDiscoveryAttempts + 8 < 8 * sizeof(QuicPacketNumber),
              "The number of MTU discovery attempts is too high");
static_assert(kPacketsBetweenMtuProbesBase < (1 << 8),
              "The initial number of packets between MTU probes is too high");

// The incresed packet size targeted when doing path MTU discovery.
const QuicByteCount kMtuDiscoveryTargetPacketSizeHigh = 1450;
const QuicByteCount kMtuDiscoveryTargetPacketSizeLow = 1430;

static_assert(kMtuDiscoveryTargetPacketSizeLow <= kMaxPacketSize,
              "MTU discovery target is too large");
static_assert(kMtuDiscoveryTargetPacketSizeHigh <= kMaxPacketSize,
              "MTU discovery target is too large");

static_assert(kMtuDiscoveryTargetPacketSizeLow > kDefaultMaxPacketSize,
              "MTU discovery target does not exceed the default packet size");
static_assert(kMtuDiscoveryTargetPacketSizeHigh > kDefaultMaxPacketSize,
              "MTU discovery target does not exceed the default packet size");

// Class that receives callbacks from the connection when frames are received
// and when other interesting events happen.
class QUIC_EXPORT_PRIVATE QuicConnectionVisitorInterface {
 public:
  virtual ~QuicConnectionVisitorInterface() {}

  // A simple visitor interface for dealing with a data frame.
  virtual void OnStreamFrame(const QuicStreamFrame& frame) = 0;

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

  // Called when the connection is closed either locally by the framer, or
  // remotely by the peer.
  virtual void OnConnectionClosed(QuicErrorCode error,
                                  const std::string& error_details,
                                  ConnectionCloseSource source) = 0;

  // Called when the connection failed to write because the socket was blocked.
  virtual void OnWriteBlocked() = 0;

  // Called once a specific QUIC version is agreed by both endpoints.
  virtual void OnSuccessfulVersionNegotiation(
      const QuicTransportVersion& version) = 0;

  // Called when a blocked socket becomes writable.
  virtual void OnCanWrite() = 0;

  // Called when the connection experiences a change in congestion window.
  virtual void OnCongestionWindowChange(QuicTime now) = 0;

  // Called when the connection receives a packet from a migrated client.
  virtual void OnConnectionMigration(PeerAddressChangeType type) = 0;

  // Called when the peer seems unreachable over the current path.
  virtual void OnPathDegrading() = 0;

  // Called after OnStreamFrame, OnRstStream, OnGoAway, OnWindowUpdateFrame,
  // OnBlockedFrame, and OnCanWrite to allow post-processing once the work has
  // been done.
  virtual void PostProcessAfterData() = 0;

  // Called when the connection sends ack after
  // kMaxConsecutiveNonRetransmittablePackets consecutive not retransmittable
  // packets sent. To instigate an ack from peer, a retransmittable frame needs
  // to be added.
  virtual void OnAckNeedsRetransmittableFrame() = 0;

  // Called to ask if the visitor wants to schedule write resumption as it both
  // has pending data to write, and is able to write (e.g. based on flow control
  // limits).
  // Writes may be pending because they were write-blocked, congestion-throttled
  // or yielded to other connections.
  virtual bool WillingAndAbleToWrite() const = 0;

  // Called to ask if any handshake messages are pending in this visitor.
  virtual bool HasPendingHandshake() const = 0;

  // Called to ask if any streams are open in this visitor, excluding the
  // reserved crypto and headers stream.
  virtual bool HasOpenDynamicStreams() const = 0;
};

// Interface which gets callbacks from the QuicConnection at interesting
// points.  Implementations must not mutate the state of the connection
// as a result of these callbacks.
class QUIC_EXPORT_PRIVATE QuicConnectionDebugVisitor
    : public QuicSentPacketManager::DebugDelegate {
 public:
  ~QuicConnectionDebugVisitor() override {}

  // Called when a packet has been sent.
  virtual void OnPacketSent(const SerializedPacket& serialized_packet,
                            QuicPacketNumber original_packet_number,
                            TransmissionType transmission_type,
                            QuicTime sent_time) {}

  // Called when a PING frame has been sent.
  virtual void OnPingSent() {}

  // Called when a packet has been received, but before it is
  // validated or parsed.
  virtual void OnPacketReceived(const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address,
                                const QuicEncryptedPacket& packet) {}

  // Called when the unauthenticated portion of the header has been parsed.
  virtual void OnUnauthenticatedHeader(const QuicPacketHeader& header) {}

  // Called when a packet is received with a connection id that does not
  // match the ID of this connection.
  virtual void OnIncorrectConnectionId(QuicConnectionId connection_id) {}

  // Called when an undecryptable packet has been received.
  virtual void OnUndecryptablePacket() {}

  // Called when a duplicate packet has been received.
  virtual void OnDuplicatePacket(QuicPacketNumber packet_number) {}

  // Called when the protocol version on the received packet doensn't match
  // current protocol version of the connection.
  virtual void OnProtocolVersionMismatch(QuicTransportVersion version) {}

  // Called when the complete header of a packet has been parsed.
  virtual void OnPacketHeader(const QuicPacketHeader& header) {}

  // Called when a StreamFrame has been parsed.
  virtual void OnStreamFrame(const QuicStreamFrame& frame) {}

  // Called when a AckFrame has been parsed.
  virtual void OnAckFrame(const QuicAckFrame& frame) {}

  // Called when a StopWaitingFrame has been parsed.
  virtual void OnStopWaitingFrame(const QuicStopWaitingFrame& frame) {}

  // Called when a QuicPaddingFrame has been parsed.
  virtual void OnPaddingFrame(const QuicPaddingFrame& frame) {}

  // Called when a Ping has been parsed.
  virtual void OnPingFrame(const QuicPingFrame& frame) {}

  // Called when a GoAway has been parsed.
  virtual void OnGoAwayFrame(const QuicGoAwayFrame& frame) {}

  // Called when a RstStreamFrame has been parsed.
  virtual void OnRstStreamFrame(const QuicRstStreamFrame& frame) {}

  // Called when a ConnectionCloseFrame has been parsed.
  virtual void OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) {}

  // Called when a WindowUpdate has been parsed.
  virtual void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame,
                                   const QuicTime& receive_time) {}

  // Called when a BlockedFrame has been parsed.
  virtual void OnBlockedFrame(const QuicBlockedFrame& frame) {}

  // Called when a public reset packet has been received.
  virtual void OnPublicResetPacket(const QuicPublicResetPacket& packet) {}

  // Called when a version negotiation packet has been received.
  virtual void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) {}

  // Called when the connection is closed.
  virtual void OnConnectionClosed(QuicErrorCode error,
                                  const std::string& error_details,
                                  ConnectionCloseSource source) {}

  // Called when the version negotiation is successful.
  virtual void OnSuccessfulVersionNegotiation(
      const QuicTransportVersion& version) {}

  // Called when a CachedNetworkParameters is sent to the client.
  virtual void OnSendConnectionState(
      const CachedNetworkParameters& cached_network_params) {}

  // Called when a CachedNetworkParameters are recieved from the client.
  virtual void OnReceiveConnectionState(
      const CachedNetworkParameters& cached_network_params) {}

  // Called when the connection parameters are set from the supplied
  // |config|.
  virtual void OnSetFromConfig(const QuicConfig& config) {}

  // Called when RTT may have changed, including when an RTT is read from
  // the config.
  virtual void OnRttChanged(QuicTime::Delta rtt) const {}
};

// QuicConnections currently use around 1KB of polymorphic types which would
// ordinarily be on the heap. Instead, store them inline in an arena.
using QuicConnectionArena = QuicOneBlockArena<1024>;

class QUIC_EXPORT_PRIVATE QuicConnectionHelperInterface {
 public:
  virtual ~QuicConnectionHelperInterface() {}

  // Returns a QuicClock to be used for all time related functions.
  virtual const QuicClock* GetClock() const = 0;

  // Returns a QuicRandom to be used for all random number related functions.
  virtual QuicRandom* GetRandomGenerator() = 0;

  // Returns a QuicBufferAllocator to be used for all stream frame buffers.
  virtual QuicBufferAllocator* GetStreamFrameBufferAllocator() = 0;

  // Returns a QuicBufferAllocator to be used for stream send buffers.
  virtual QuicBufferAllocator* GetStreamSendBufferAllocator() = 0;
};

class QUIC_EXPORT_PRIVATE QuicConnection
    : public QuicFramerVisitorInterface,
      public QuicBlockedWriterInterface,
      public QuicPacketGenerator::DelegateInterface,
      public QuicSentPacketManager::NetworkChangeVisitor {
 public:
  enum AckBundling {
    // Send an ack if it's already queued in the connection.
    SEND_ACK_IF_QUEUED,
    // Always send an ack.
    SEND_ACK,
    // Bundle an ack with outgoing data.
    SEND_ACK_IF_PENDING,
    // Do not send ack.
    NO_ACK,
  };

  enum AckMode { TCP_ACKING, ACK_DECIMATION, ACK_DECIMATION_WITH_REORDERING };

  // Constructs a new QuicConnection for |connection_id| and |address| using
  // |writer| to write packets. |owns_writer| specifies whether the connection
  // takes ownership of |writer|. |helper| must outlive this connection.
  QuicConnection(QuicConnectionId connection_id,
                 QuicSocketAddress address,
                 QuicConnectionHelperInterface* helper,
                 QuicAlarmFactory* alarm_factory,
                 QuicPacketWriter* writer,
                 bool owns_writer,
                 Perspective perspective,
                 const QuicTransportVersionVector& supported_versions);
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

  // Sets the number of active streams on the connection for congestion control.
  void SetNumOpenStreams(size_t num_streams);

  // Send the data in |data| to the peer in as few packets as possible.
  // Returns a pair with the number of bytes consumed from data, and a boolean
  // indicating if the fin bit was consumed.  This does not indicate the data
  // has been sent on the wire: it may have been turned into a packet and queued
  // if the socket was unexpectedly blocked.
  // If |listener| is provided, then it will be informed once ACKs have been
  // received for all the packets written in this call.
  // The |listener| is not owned by the QuicConnection and must outlive it.
  virtual QuicConsumedData SendStreamData(
      QuicStreamId id,
      QuicIOVector iov,
      QuicStreamOffset offset,
      StreamSendingState state,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  // Send a RST_STREAM frame to the peer.
  virtual void SendRstStream(QuicStreamId id,
                             QuicRstStreamErrorCode error,
                             QuicStreamOffset bytes_written);

  // Send a BLOCKED frame to the peer.
  virtual void SendBlocked(QuicStreamId id);

  // Send a WINDOW_UPDATE frame to the peer.
  virtual void SendWindowUpdate(QuicStreamId id, QuicStreamOffset byte_offset);

  // Closes the connection.
  // |connection_close_behavior| determines whether or not a connection close
  // packet is sent to the peer.
  virtual void CloseConnection(
      QuicErrorCode error,
      const std::string& details,
      ConnectionCloseBehavior connection_close_behavior);

  // Sends a GOAWAY frame. Does nothing if a GOAWAY frame has already been sent.
  virtual void SendGoAway(QuicErrorCode error,
                          QuicStreamId last_good_stream_id,
                          const std::string& reason);

  // Returns statistics tracked for this connection.
  const QuicConnectionStats& GetStats();

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

  // Called when the caller thinks it's worth a try to write.
  virtual void OnCanWrite();

  // Called when an error occurs while attempting to write a packet to the
  // network.
  void OnWriteError(int error_code);

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

  // The QuicVersionLabel for the version this connection is using.
  QuicVersionLabel version_label() const {
    return framer_.last_version_label();
  }

  // The versions of the protocol that this connection supports.
  const QuicTransportVersionVector& supported_versions() const {
    return framer_.supported_versions();
  }

  // From QuicFramerVisitorInterface
  void OnError(QuicFramer* framer) override;
  bool OnProtocolVersionMismatch(
      QuicTransportVersion received_version) override;
  void OnPacket() override;
  void OnPublicResetPacket(const QuicPublicResetPacket& packet) override;
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) override;
  bool OnUnauthenticatedPublicHeader(
      const QuicPacketPublicHeader& header) override;
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override;
  void OnDecryptedPacket(EncryptionLevel level) override;
  bool OnPacketHeader(const QuicPacketHeader& header) override;
  bool OnStreamFrame(const QuicStreamFrame& frame) override;
  bool OnAckFrame(const QuicAckFrame& frame) override;
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override;
  bool OnPaddingFrame(const QuicPaddingFrame& frame) override;
  bool OnPingFrame(const QuicPingFrame& frame) override;
  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override;
  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override;
  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override;
  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override;
  bool OnBlockedFrame(const QuicBlockedFrame& frame) override;
  void OnPacketComplete() override;

  // QuicConnectionCloseDelegateInterface
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& error_details,
                            ConnectionCloseSource source) override;

  // QuicPacketGenerator::DelegateInterface
  bool ShouldGeneratePacket(HasRetransmittableData retransmittable,
                            IsHandshake handshake) override;
  const QuicFrame GetUpdatedAckFrame() override;
  void PopulateStopWaitingFrame(QuicStopWaitingFrame* stop_waiting) override;

  // QuicPacketCreator::DelegateInterface
  void OnSerializedPacket(SerializedPacket* packet) override;

  // QuicSentPacketManager::NetworkChangeVisitor
  void OnCongestionChange() override;
  void OnPathDegrading() override;
  void OnPathMtuIncreased(QuicPacketLength packet_size) override;

  // Called by the crypto stream when the handshake completes. In the server's
  // case this is when the SHLO has been ACKed. Clients call this on receipt of
  // the SHLO.
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
  const QuicTime::Delta ping_timeout() { return ping_timeout_; }
  // Used in Chromium, but not internally.
  void set_creator_debug_delegate(QuicPacketCreator::DebugDelegate* visitor) {
    packet_generator_.set_debug_delegate(visitor);
  }
  const QuicSocketAddress& self_address() const { return self_address_; }
  const QuicSocketAddress& peer_address() const { return peer_address_; }
  QuicConnectionId connection_id() const { return connection_id_; }
  const QuicClock* clock() const { return clock_; }
  QuicRandom* random_generator() const { return random_generator_; }
  QuicByteCount max_packet_length() const;
  void SetMaxPacketLength(QuicByteCount length);

  size_t mtu_probe_count() const { return mtu_probe_count_; }

  bool connected() const { return connected_; }

  bool goaway_sent() const { return goaway_sent_; }

  bool goaway_received() const { return goaway_received_; }

  // Must only be called on client connections.
  const QuicTransportVersionVector& server_supported_versions() const {
    DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
    return server_supported_versions_;
  }

  // Testing only.
  size_t NumQueuedPackets() const { return queued_packets_.size(); }

  // Once called, any sent crypto packets to be saved as the
  // termination packet, for use with stateless rejections.
  void EnableSavingCryptoPackets();

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

  // Sends a ping frame.
  void SendPing();

  // Sets up a packet with an QuicAckFrame and sends it out.
  void SendAck();

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

  // Changes the encrypter used for level |level| to |encrypter|. The function
  // takes ownership of |encrypter|.
  void SetEncrypter(EncryptionLevel level, QuicEncrypter* encrypter);

  // SetNonceForPublicHeader sets the nonce that will be transmitted in the
  // public header of each packet encrypted at the initial encryption level
  // decrypted. This should only be called on the server side.
  void SetDiversificationNonce(const DiversificationNonce& nonce);

  // SetDefaultEncryptionLevel sets the encryption level that will be applied
  // to new packets.
  void SetDefaultEncryptionLevel(EncryptionLevel level);

  // SetDecrypter sets the primary decrypter, replacing any that already exists,
  // and takes ownership. If an alternative decrypter is in place then the
  // function DCHECKs. This is intended for cases where one knows that future
  // packets will be using the new decrypter and the previous decrypter is now
  // obsolete. |level| indicates the encryption level of the new decrypter.
  void SetDecrypter(EncryptionLevel level, QuicDecrypter* decrypter);

  // SetAlternativeDecrypter sets a decrypter that may be used to decrypt
  // future packets and takes ownership of it. |level| indicates the encryption
  // level of the decrypter. If |latch_once_used| is true, then the first time
  // that the decrypter is successful it will replace the primary decrypter.
  // Otherwise both decrypters will remain active and the primary decrypter
  // will be the one last used.
  void SetAlternativeDecrypter(EncryptionLevel level,
                               QuicDecrypter* decrypter,
                               bool latch_once_used);

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

  bool CanWrite(HasRetransmittableData retransmittable);

  // Stores current batch state for connection, puts the connection
  // into batch mode, and destruction restores the stored batch state.
  // While the bundler is in scope, any generated frames are bundled
  // as densely as possible into packets.  In addition, this bundler
  // can be configured to ensure that an ACK frame is included in the
  // first packet created, if there's new ack information to be sent.
  class QUIC_EXPORT_PRIVATE ScopedPacketBundler {
   public:
    // In addition to all outgoing frames being bundled when the
    // bundler is in scope, setting |include_ack| to true ensures that
    // an ACK frame is opportunistically bundled with the first
    // outgoing packet.
    ScopedPacketBundler(QuicConnection* connection, AckBundling send_ack);
    ~ScopedPacketBundler();

   private:
    bool ShouldSendAck(AckBundling ack_mode) const;

    QuicConnection* connection_;
    bool already_in_batch_mode_;
  };

  // Delays setting the retransmission alarm until the scope is exited.
  // When nested, only the outermost scheduler will set the alarm, and inner
  // ones have no effect.
  class QUIC_EXPORT_PRIVATE ScopedRetransmissionScheduler {
   public:
    explicit ScopedRetransmissionScheduler(QuicConnection* connection);
    ~ScopedRetransmissionScheduler();

   private:
    QuicConnection* connection_;
    // Set to the connection's delay_setting_retransmission_alarm_ value in the
    // constructor and when true, causes this class to do nothing.
    const bool already_delayed_;
  };

  QuicPacketWriter* writer() { return writer_; }
  const QuicPacketWriter* writer() const { return writer_; }

  // Sends an MTU discovery packet of size |target_mtu|.  If the packet is
  // acknowledged by the peer, the maximum packet size will be increased to
  // |target_mtu|.
  void SendMtuDiscoveryPacket(QuicByteCount target_mtu);

  // Sends an MTU discovery packet of size |mtu_discovery_target_| and updates
  // the MTU discovery alarm.
  void DiscoverMtu();

  // Sets the stream notifer on the SentPacketManager.
  void SetStreamNotifier(StreamNotifierInterface* stream_notifier);

  // Set data producer in framer.
  void SetDataProducer(QuicStreamFrameDataProducer* data_producer);

  // Return the id of the cipher of the primary decrypter of the framer.
  uint32_t cipher_id() const { return framer_.decrypter()->cipher_id(); }

  std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets() {
    return termination_packets_.get();
  }

  bool ack_queued() const { return ack_queued_; }

  bool ack_frame_updated() const;

  QuicConnectionHelperInterface* helper() { return helper_; }
  QuicAlarmFactory* alarm_factory() { return alarm_factory_; }

  QuicStringPiece GetCurrentPacket();

  const QuicPacketGenerator& packet_generator() const {
    return packet_generator_;
  }

  const QuicReceivedPacketManager& received_packet_manager() const {
    return received_packet_manager_;
  }

  EncryptionLevel encryption_level() const { return encryption_level_; }

  const QuicSocketAddress& last_packet_source_address() const {
    return last_packet_source_address_;
  }

 protected:
  // Calls cancel() on all the alarms owned by this connection.
  void CancelAllAlarms();

  // Send a packet to the peer, and takes ownership of the packet if the packet
  // cannot be written immediately.
  virtual void SendOrQueuePacket(SerializedPacket* packet);

  // Called after a packet is received from a new peer address and is decrypted.
  // Starts validation of peer's address change.
  virtual void StartPeerMigration(PeerAddressChangeType peer_migration_type);

  // Called when a peer address migration is validated.
  virtual void OnPeerMigrationValidated();

  // Selects and updates the version of the protocol being used by selecting a
  // version from |available_versions| which is also supported. Returns true if
  // such a version exists, false otherwise.
  bool SelectMutualVersion(
      const QuicTransportVersionVector& available_versions);

  // Returns the current per-packet options for the connection.
  PerPacketOptions* per_packet_options() { return per_packet_options_; }
  // Sets the current per-packet options for the connection. The QuicConnection
  // does not take ownership of |options|; |options| must live for as long as
  // the QuicConnection is in use.
  void set_per_packet_options(PerPacketOptions* options) {
    per_packet_options_ = options;
  }

  // If |defer| is true, configures the connection to defer sending packets in
  // response to an ACK to the SendAlarm. If |defer| is false, packets may be
  // sent immediately after receiving an ACK.
  void set_defer_send_in_response_to_packets(bool defer) {
    defer_send_in_response_to_packets_ = defer;
  }

  PeerAddressChangeType active_peer_migration_type() {
    return active_peer_migration_type_;
  }

  // Sends the connection close packet to the peer. |ack_mode| determines
  // whether ack frame will be bundled with the connection close packet.
  virtual void SendConnectionClosePacket(QuicErrorCode error,
                                         const std::string& details,
                                         AckBundling ack_mode);

  // Returns true if the packet should be discarded and not sent.
  virtual bool ShouldDiscardPacket(const SerializedPacket& packet);

  // Returns true if this connection allows self address change.
  virtual bool AllowSelfAddressChange() const;

  // Called when a self address change is observed.
  virtual void OnSelfAddressChange() {}

 private:
  friend class test::QuicConnectionPeer;
  friend class test::PacketSavingConnection;

  typedef std::list<SerializedPacket> QueuedPacketList;

  // Notifies the visitor of the close and marks the connection as disconnected.
  // Does not send a connection close frame to the peer.
  void TearDownLocalConnectionState(QuicErrorCode error,
                                    const std::string& details,
                                    ConnectionCloseSource source);

  // Writes the given packet to socket, encrypted with packet's
  // encryption_level. Returns true on successful write, and false if the writer
  // was blocked and the write needs to be tried again. Notifies the
  // SentPacketManager when the write is successful and sets
  // retransmittable frames to nullptr.
  // Saves the connection close packet for later transmission, even if the
  // writer is write blocked.
  bool WritePacket(SerializedPacket* packet);

  // Make sure an ack we got from our peer is sane.
  // Returns nullptr for valid acks or an error string if it was invalid.
  const char* ValidateAckFrame(const QuicAckFrame& incoming_ack);

  // Make sure a stop waiting we got from our peer is sane.
  // Returns nullptr if the frame is valid or an error string if it was invalid.
  const char* ValidateStopWaitingFrame(
      const QuicStopWaitingFrame& stop_waiting);

  // Sends a version negotiation packet to the peer.
  void SendVersionNegotiationPacket();

  // Clears any accumulated frames from the last received packet.
  void ClearLastFrames();

  // Deletes and clears any queued packets.
  void ClearQueuedPackets();

  // Writes as many queued packets as possible.  The connection must not be
  // blocked when this is called.
  void WriteQueuedPackets();

  // Writes as many pending retransmissions as possible.
  void WritePendingRetransmissions();

  // Queues |packet| in the hopes that it can be decrypted in the
  // future, when a new key is installed.
  void QueueUndecryptablePacket(const QuicEncryptedPacket& packet);

  // Attempts to process any queued undecryptable packets.
  void MaybeProcessUndecryptablePackets();

  // Sends any packets which are a response to the last packet, including both
  // acks and pending writes if an ack opened the congestion window.
  void MaybeSendInResponseToPacket();

  // Queue an ack or set the ack alarm if needed.  |was_missing| is true if
  // the most recently received packet was formerly missing.
  void MaybeQueueAck(bool was_missing);

  // Gets the least unacked packet number, which is the next packet number to be
  // sent if there are no outstanding packets.
  QuicPacketNumber GetLeastUnacked() const;

  // Sets the timeout alarm to the appropriate value, if any.
  void SetTimeoutAlarm();

  // Sets the ping alarm to the appropriate value, if any.
  void SetPingAlarm();

  // Sets the retransmission alarm based on SentPacketManager.
  void SetRetransmissionAlarm();

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

  // Consider receiving crypto frame on non crypto stream as memory corruption.
  bool MaybeConsiderAsMemoryCorruption(const QuicStreamFrame& frame);

  const QuicTime::Delta DelayedAckTime();

  // Check if the connection has no outstanding data to send and notify
  // congestion controller if it is the case.
  void CheckIfApplicationLimited();

  QuicFramer framer_;
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

  const QuicConnectionId connection_id_;
  // Address on the last successfully processed packet received from the
  // client.
  QuicSocketAddress self_address_;
  QuicSocketAddress peer_address_;

  // Records change type when the peer initiates migration to a new peer
  // address. Reset to NO_CHANGE after peer migration is validated.
  PeerAddressChangeType active_peer_migration_type_;

  // Records highest sent packet number when peer migration is started.
  QuicPacketNumber highest_packet_sent_before_peer_migration_;

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
  // Whether the most recent packet was missing before it was received.
  bool was_last_packet_missing_;

  // Track some peer state so we can do less bookkeeping
  // Largest sequence sent by the peer which had an ack frame (latest ack info).
  QuicPacketNumber largest_seen_packet_with_ack_;

  // Largest packet number sent by the peer which had a stop waiting frame.
  QuicPacketNumber largest_seen_packet_with_stop_waiting_;

  // Collection of packets which were received before encryption was
  // established, but which could not be decrypted.  We buffer these on
  // the assumption that they could not be processed because they were
  // sent with the INITIAL encryption and the CHLO message was lost.
  QuicDeque<std::unique_ptr<QuicEncryptedPacket>> undecryptable_packets_;

  // Maximum number of undecryptable packets the connection will store.
  size_t max_undecryptable_packets_;

  // When the version negotiation packet could not be sent because the socket
  // was not writable, this is set to true.
  bool pending_version_negotiation_packet_;

  // When packets could not be sent because the socket was not writable,
  // they are added to this list.  All corresponding frames are in
  // unacked_packets_ if they are to be retransmitted.  Packets encrypted_buffer
  // fields are owned by the QueuedPacketList, in order to ensure they outlast
  // the original scope of the SerializedPacket.
  QueuedPacketList queued_packets_;

  // If true, then crypto packets will be saved as termination packets.
  bool save_crypto_packets_as_termination_packets_;

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
  // When true, close the QUIC connection when there are no open streams after
  // 3 consecutive RTOs.
  bool close_connection_after_three_rtos_;

  QuicReceivedPacketManager received_packet_manager_;

  // Indicates whether an ack should be sent the next time we try to write.
  bool ack_queued_;
  // How many retransmittable packets have arrived without sending an ack.
  QuicPacketCount num_retransmittable_packets_received_since_last_ack_sent_;
  // Whether there were missing packets in the last sent ack.
  bool last_ack_had_missing_packets_;
  // How many consecutive packets have arrived without sending an ack.
  QuicPacketCount num_packets_received_since_last_ack_sent_;
  // Indicates how many consecutive times an ack has arrived which indicates
  // the peer needs to stop waiting for some packets.
  int stop_waiting_count_;
  // Indicates the current ack mode, defaults to acking every 2 packets.
  AckMode ack_mode_;
  // The max delay in fraction of min_rtt to use when sending decimated acks.
  float ack_decimation_delay_;
  // When true, removes ack decimation's max number of packets(10) before
  // sending an ack.
  bool unlimited_ack_decimation_;

  // Indicates the retransmit alarm is going to be set by the
  // ScopedRetransmitAlarmDelayer
  bool delay_setting_retransmission_alarm_;
  // Indicates the retransmission alarm needs to be set.
  bool pending_retransmission_alarm_;

  // If true, defer sending data in response to received packets to the
  // SendAlarm.
  bool defer_send_in_response_to_packets_;

  // The timeout for PING.
  QuicTime::Delta ping_timeout_;

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
  // TODO(ianswett): Remove resume_writes_alarm when deprecating
  // FLAGS_quic_reloadable_flag_quic_only_one_sending_alarm
  QuicArenaScopedPtr<QuicAlarm> resume_writes_alarm_;
  // An alarm that fires when the connection may have timed out.
  QuicArenaScopedPtr<QuicAlarm> timeout_alarm_;
  // An alarm that fires when a ping should be sent.
  QuicArenaScopedPtr<QuicAlarm> ping_alarm_;
  // An alarm that fires when an MTU probe should be sent.
  QuicArenaScopedPtr<QuicAlarm> mtu_discovery_alarm_;

  // Neither visitor is owned by this class.
  QuicConnectionVisitorInterface* visitor_;
  QuicConnectionDebugVisitor* debug_visitor_;

  QuicPacketGenerator packet_generator_;

  // Network idle time before this connection is closed.
  QuicTime::Delta idle_network_timeout_;
  // The connection will wait this long for the handshake to complete.
  QuicTime::Delta handshake_timeout_;

  // Statistics for this session.
  QuicConnectionStats stats_;

  // The time that we got a packet for this connection.
  // This is used for timeouts, and does not indicate the packet was processed.
  QuicTime time_of_last_received_packet_;

  // The last time this connection began sending a new (non-retransmitted)
  // packet.
  QuicTime time_of_last_sent_new_packet_;

  // The the send time of the first retransmittable packet sent after
  // |time_of_last_received_packet_|.
  QuicTime last_send_for_timeout_;

  // Sent packet manager which tracks the status of packets sent by this
  // connection and contains the send and receive algorithms to determine when
  // to send packets.
  QuicSentPacketManager sent_packet_manager_;

  // The state of connection in version negotiation finite state machine.
  enum QuicVersionNegotiationState {
    START_NEGOTIATION = 0,
    // Server-side this implies we've sent a version negotiation packet and are
    // waiting on the client to select a compatible version.  Client-side this
    // implies we've gotten a version negotiation packet, are retransmitting the
    // initial packets with a supported version and are waiting for our first
    // packet from the server.
    NEGOTIATION_IN_PROGRESS,
    // This indicates this endpoint has received a packet from the peer with a
    // version this endpoint supports.  Version negotiation is complete, and the
    // version number will no longer be sent with future packets.
    NEGOTIATED_VERSION
  };
  QuicVersionNegotiationState version_negotiation_state_;

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
  QuicTransportVersionVector server_supported_versions_;

  // The size of the packet we are targeting while doing path MTU discovery.
  QuicByteCount mtu_discovery_target_;

  // The number of MTU probes already sent.
  size_t mtu_probe_count_;

  // The number of packets between MTU probes.
  QuicPacketCount packets_between_mtu_probes_;

  // The packet number of the packet after which the next MTU probe will be
  // sent.
  QuicPacketNumber next_mtu_probe_at_;

  // The value of the MTU regularly used by the connection. This is different
  // from the value returned by max_packet_size(), as max_packet_size() returns
  // the value of the MTU as currently used by the serializer, so if
  // serialization of an MTU probe is in progress, those two values will be
  // different.
  QuicByteCount long_term_mtu_;

  // The size of the largest packet received from peer.
  QuicByteCount largest_received_packet_size_;

  // Whether a GoAway has been sent.
  bool goaway_sent_;

  // Whether a GoAway has been received.
  bool goaway_received_;

  // Indicates whether a write error is encountered currently. This is used to
  // avoid infinite write errors.
  bool write_error_occurred_;

  // Indicates not to send or process stop waiting frames.
  bool no_stop_waiting_frames_;

  // Consecutive number of sent packets which have no retransmittable frames.
  size_t consecutive_num_packets_with_no_retransmittable_frames_;

  DISALLOW_COPY_AND_ASSIGN(QuicConnection);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_CONNECTION_H_
