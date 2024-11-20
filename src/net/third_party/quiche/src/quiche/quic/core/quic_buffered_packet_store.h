// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_
#define QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_dispatcher_stats.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_packet_number.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream_frame_data_producer.h"
#include "quiche/quic/core/quic_stream_send_buffer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/tls_chlo_extractor.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_intrusive_list.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

namespace test {
class QuicBufferedPacketStorePeer;
}  // namespace test

// This class buffers packets for each connection until either
// 1) They are requested to be delivered via
//    DeliverPacket()/DeliverPacketsForNextConnection(), or
// 2) They expire after exceeding their lifetime in the store.
//
// It can only buffer packets on certain number of connections. It has two pools
// of connections: connections with CHLO buffered and those without CHLO. The
// latter has its own upper limit along with the max number of connections this
// store can hold. The former pool can grow till this store is full.
class QUICHE_EXPORT QuicBufferedPacketStore {
 public:
  enum EnqueuePacketResult {
    SUCCESS = 0,
    // Too many packets stored up for a certain connection.
    TOO_MANY_PACKETS,
    // Too many connections stored up in the store.
    TOO_MANY_CONNECTIONS,
    // Replaced CID collide with a buffered or active session.
    CID_COLLISION,
  };

  struct QUICHE_EXPORT BufferedPacket {
    BufferedPacket(std::unique_ptr<QuicReceivedPacket> packet,
                   QuicSocketAddress self_address,
                   QuicSocketAddress peer_address, bool is_ietf_initial_packet);
    BufferedPacket(BufferedPacket&& other);

    BufferedPacket& operator=(BufferedPacket&& other);

    ~BufferedPacket();

    std::unique_ptr<QuicReceivedPacket> packet;
    QuicSocketAddress self_address;
    QuicSocketAddress peer_address;
    bool is_ietf_initial_packet;
  };

  // A queue of BufferedPackets for a connection.
  struct QUICHE_EXPORT BufferedPacketList {
    BufferedPacketList();
    BufferedPacketList(BufferedPacketList&& other);

    BufferedPacketList& operator=(BufferedPacketList&& other);

    ~BufferedPacketList();

    bool HasReplacedConnectionId() const {
      return replaced_connection_id.has_value() &&
             !replaced_connection_id->IsEmpty();
    }

    bool HasAttemptedToReplaceConnectionId() const {
      return connection_id_generator != nullptr;
    }

    void SetAttemptedToReplaceConnectionId(
        ConnectionIdGeneratorInterface* generator) {
      connection_id_generator = generator;
    }

    QuicPacketNumber GetLastSentPacketNumber() const {
      if (dispatcher_sent_packets.empty()) {
        return QuicPacketNumber();
      }
      return dispatcher_sent_packets.back().packet_number;
    }

    std::list<BufferedPacket> buffered_packets;
    QuicTime creation_time;
    // |parsed_chlo| is set iff the entire CHLO has been received.
    std::optional<ParsedClientHello> parsed_chlo;
    // Indicating whether this is an IETF QUIC connection.
    bool ietf_quic;
    // If buffered_packets contains the CHLO, it is the version of the CHLO.
    // Otherwise, it is the version of the first packet in |buffered_packets|.
    ParsedQuicVersion version;
    TlsChloExtractor tls_chlo_extractor;
    // Only one reference to the generator is stored per connection, and this is
    // stored when the replaced CID is generated.
    ConnectionIdGeneratorInterface* connection_id_generator = nullptr;
    // The original connection ID of the connection.
    QuicConnectionId original_connection_id;
    // Set to the result of ConnectionIdGenerator::MaybeReplaceConnectionId,
    // when the first IETF INITIAL packet is enqueued.
    // Note that std::nullopt indicates one the following cases, you can use
    // HasAttemptedToReplaceConnectionId() to distinguish them:
    // 1. No attempt to replace CID has been made.
    // 2. One attempt to replace CID has been made, but the CID generator does
    //    not want to replace it.
    std::optional<QuicConnectionId> replaced_connection_id;
    // All ACK packets sent by the dispatcher, ordered by sending packet number.
    absl::InlinedVector<DispatcherSentPacket, 2> dispatcher_sent_packets;
  };

  // Tag type for the list of sessions with full CHLO buffered.
  struct QUICHE_EXPORT BufferedSessionsWithChloList {};

  // The internal data structure for a buffered session.
  struct QUICHE_EXPORT BufferedPacketListNode
      : public quiche::QuicheIntrusiveLink<BufferedPacketListNode>,
        public quiche::QuicheIntrusiveLink<BufferedPacketListNode,
                                           BufferedSessionsWithChloList>,
        public std::enable_shared_from_this<BufferedPacketListNode>,
        public BufferedPacketList {};

  class QUICHE_EXPORT VisitorInterface {
   public:
    virtual ~VisitorInterface() {}

    // Called for each expired connection when alarm fires.
    virtual void OnExpiredPackets(QuicConnectionId connection_id,
                                  BufferedPacketList early_arrived_packets) = 0;

    enum class HandleCidCollisionResult {
      kOk,
      kCollision,
    };
    // Check and handle CID collision for |replaced_connection_id|.
    // This method is called immediately after |replaced_connection_id| is
    // generated by the connection ID generator, at which time the mapping from
    // |replaced_connection_id| to the connection is not yet established, which
    // means if the implementation calls
    //   store.HasBufferedPackets(replaced_connection_id);
    // and it returns true, then |replaced_connection_id| has already been
    // mapped to another connection, i.e. a CID collision.
    virtual HandleCidCollisionResult HandleConnectionIdCollision(
        const QuicConnectionId& original_connection_id,
        const QuicConnectionId& replaced_connection_id,
        const QuicSocketAddress& self_address,
        const QuicSocketAddress& peer_address, ParsedQuicVersion version,
        const ParsedClientHello* parsed_chlo) = 0;
  };

  QuicBufferedPacketStore(VisitorInterface* visitor, const QuicClock* clock,
                          QuicAlarmFactory* alarm_factory,
                          QuicDispatcherStats& stats);

  QuicBufferedPacketStore(const QuicBufferedPacketStore&) = delete;

  ~QuicBufferedPacketStore();

  QuicBufferedPacketStore& operator=(const QuicBufferedPacketStore&) = delete;

  void set_writer(QuicPacketWriter* writer) { writer_ = writer; }

  // Adds a copy of packet into the packet queue for given connection. If the
  // packet is the last one of the CHLO, |parsed_chlo| will contain a parsed
  // version of the CHLO. |connection_id_generator| is the Connection ID
  // Generator to use with the connection.
  // Returns SUCCESS iff the packet is successfully enqueued, in that case the
  // function may attempt to send an ACK if the enqueued packet is an IETF
  // Initial packet.
  EnqueuePacketResult EnqueuePacket(
      const ReceivedPacketInfo& packet_info,
      std::optional<ParsedClientHello> parsed_chlo,
      ConnectionIdGeneratorInterface& connection_id_generator);

  // Returns true if there are any packets buffered for |connection_id|.
  // |connection_id| can be either original or replaced connection ID.
  bool HasBufferedPackets(QuicConnectionId connection_id) const;

  // Ingests this packet into the corresponding TlsChloExtractor. This should
  // only be called when HasBufferedPackets(connection_id) is true.
  // Returns whether we've now parsed a full multi-packet TLS CHLO.
  // When this returns true, |out_supported_groups| is populated with the list
  // of groups in the CHLO's 'supported_groups' TLS extension. |out_alpns| is
  // populated with the list of ALPNs extracted from the CHLO. |out_sni| is
  // populated with the SNI tag in CHLO. |out_resumption_attempted| is populated
  // if the CHLO has the 'pre_shared_key' TLS extension.
  // |out_early_data_attempted| is populated if the CHLO has the 'early_data'
  // TLS extension. When this returns false, and an unrecoverable error happened
  // due to a TLS alert, |*tls_alert| will be set to the alert value.
  bool IngestPacketForTlsChloExtraction(
      const QuicConnectionId& connection_id, const ParsedQuicVersion& version,
      const QuicReceivedPacket& packet,
      std::vector<uint16_t>* out_supported_groups,
      std::vector<uint16_t>* out_cert_compression_algos,
      std::vector<std::string>* out_alpns, std::string* out_sni,
      bool* out_resumption_attempted, bool* out_early_data_attempted,
      std::optional<uint8_t>* tls_alert);

  // Returns the list of buffered packets for |connection_id| and removes them
  // from the store. Returns an empty list if no early arrived packets for this
  // connection are present.
  // |connection_id| can be either original or replaced connection ID.
  BufferedPacketList DeliverPackets(QuicConnectionId connection_id);

  // Discards packets buffered for |connection_id|, if any.
  // |connection_id| can be either original or replaced connection ID.
  void DiscardPackets(QuicConnectionId connection_id);

  // Discards all the packets.
  void DiscardAllPackets();

  // Examines how long packets have been buffered in the store for each
  // connection. If they stay too long, removes them for new coming packets and
  // calls |visitor_|'s OnPotentialConnectionExpire().
  // Resets the alarm at the end.
  void OnExpirationTimeout();

  // Delivers buffered packets for next connection with CHLO to open.
  // Return connection id for next connection in |connection_id|
  // and all buffered packets including CHLO.
  // The returned list should at least has one packet(CHLO) if
  // store does have any connection to open. If no connection in the store has
  // received CHLO yet, empty list will be returned.
  BufferedPacketList DeliverPacketsForNextConnection(
      QuicConnectionId* connection_id);

  // Is the given connection in the store and contains the full CHLO?
  // |connection_id| can be either original or replaced connection ID.
  bool HasChloForConnection(QuicConnectionId connection_id);

  // Is there any connection in the store that contains a full CHLO?
  bool HasChlosBuffered() const;

  // Returns the BufferedPacketList for |connection_id|, returns
  // nullptr if not found.
  const BufferedPacketList* GetPacketList(
      const QuicConnectionId& connection_id) const;

  bool ack_buffered_initial_packets() const {
    return ack_buffered_initial_packets_;
  }

 private:
  friend class test::QuicBufferedPacketStorePeer;

  // Set expiration alarm if it hasn't been set.
  void MaybeSetExpirationAlarm();

  // Return true if add an extra packet will go beyond allowed max connection
  // limit. The limit for non-CHLO packet and CHLO packet is different.
  bool ShouldNotBufferPacket(bool is_chlo) const;

  // Remove |node| from the buffered store. If caller wants to access |node|
  // after this call, it should use a shared_ptr<BufferedPacketListNode> to keep
  // |node| alive:
  //
  //   BufferedPacketListNode& node = ...;
  //   auto node_ref = node.shared_from_this();
  //   RemoveFromStore(node);
  //   |node| can still be used here.
  //
  void RemoveFromStore(BufferedPacketListNode& node);

  // Debug helper to check invariants that need to be true for |packet_list|,
  // assuming |packet_list| is in |buffer_session_map_|. Returns true if all
  // invariants are true, and false otherwise.
  // The checked invariants are:
  // - |packet_list| is correctly mapped with original and replaced connection
  //   IDs.
  // - |packet_list| contains a |parsed_chlo| iff it is in the
  //   |buffered_sessions_with_chlo_| list.
  bool CheckInvariants(const BufferedPacketList& packet_list) const;

  // If |packet_info.packet| is a valid IETF INITIAL packet, reply with an
  // INITIAL, ack-only packet. |packet_list| will be used to
  // - Provide information needed to create the ack-only packet, and
  // - Store the information of the sent ack-only packet into
  //   |packet_list.dispatcher_sent_packets|.
  void MaybeAckInitialPacket(const ReceivedPacketInfo& packet_info,
                             BufferedPacketList& packet_list);

  QuicDispatcherStats& stats_;

  // Map from connection ID to the list of buffered packets for that connection.
  // The key can be either the original or the replaced connection ID.
  // The value is never nullptr.
  absl::flat_hash_map<QuicConnectionId, std::shared_ptr<BufferedPacketListNode>,
                      QuicConnectionIdHash>
      buffered_session_map_;

  // Main list of all buffered sessions, in insertion order.
  quiche::QuicheIntrusiveList<BufferedPacketListNode> buffered_sessions_;
  size_t num_buffered_sessions_ = 0;

  // Secondary list of all buffered sessions with full CHLO.
  quiche::QuicheIntrusiveList<BufferedPacketListNode,
                              BufferedSessionsWithChloList>
      buffered_sessions_with_chlo_;
  size_t num_buffered_sessions_with_chlo_ = 0;

  // The max time the packets of a connection can be buffer in the store.
  const QuicTime::Delta connection_life_span_;

  VisitorInterface* visitor_;  // Unowned.

  const QuicClock* clock_;  // Unowned.

  QuicPacketWriter* writer_ = nullptr;  // Unowned.

  // This alarm fires every |connection_life_span_| to clean up
  // packets staying in the store for too long.
  std::unique_ptr<QuicAlarm> expiration_alarm_;

  const bool ack_buffered_initial_packets_ =
      GetQuicRestartFlag(quic_dispatcher_ack_buffered_initial_packets);
};

// Collects packets serialized by a QuicPacketCreator.
class QUICHE_NO_EXPORT PacketCollector
    : public QuicPacketCreator::DelegateInterface,
      public QuicStreamFrameDataProducer {
 public:
  explicit PacketCollector(quiche::QuicheBufferAllocator* allocator)
      : send_buffer_(allocator) {}
  ~PacketCollector() override = default;

  // QuicPacketCreator::DelegateInterface methods:
  void OnSerializedPacket(SerializedPacket serialized_packet) override {
    // Make a copy of the serialized packet to send later.
    packets_.emplace_back(
        new QuicEncryptedPacket(CopyBuffer(serialized_packet),
                                serialized_packet.encrypted_length, true));
  }

  QuicPacketBuffer GetPacketBuffer() override {
    // Let QuicPacketCreator to serialize packets on stack buffer.
    return {nullptr, nullptr};
  }

  void OnUnrecoverableError(QuicErrorCode /*error*/,
                            const std::string& /*error_details*/) override {}

  bool ShouldGeneratePacket(HasRetransmittableData /*retransmittable*/,
                            IsHandshake /*handshake*/) override {
    QUICHE_DCHECK(false);
    return true;
  }

  void MaybeBundleOpportunistically(
      TransmissionType /*transmission_type*/) override {
    QUICHE_DCHECK(false);
  }

  QuicByteCount GetFlowControlSendWindowSize(QuicStreamId /*id*/) override {
    QUICHE_DCHECK(false);
    return std::numeric_limits<QuicByteCount>::max();
  }

  SerializedPacketFate GetSerializedPacketFate(
      bool /*is_mtu_discovery*/,
      EncryptionLevel /*encryption_level*/) override {
    return SEND_TO_WRITER;
  }

  // QuicStreamFrameDataProducer
  WriteStreamDataResult WriteStreamData(QuicStreamId /*id*/,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        QuicDataWriter* writer) override {
    if (send_buffer_.WriteStreamData(offset, data_length, writer)) {
      return WRITE_SUCCESS;
    }
    return WRITE_FAILED;
  }
  bool WriteCryptoData(EncryptionLevel /*level*/, QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override {
    return send_buffer_.WriteStreamData(offset, data_length, writer);
  }

  std::vector<std::unique_ptr<QuicEncryptedPacket>>* packets() {
    return &packets_;
  }

 private:
  std::vector<std::unique_ptr<QuicEncryptedPacket>> packets_;
  // This is only needed until the packets are encrypted. Once packets are
  // encrypted, the stream data is no longer required.
  QuicStreamSendBuffer send_buffer_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_
