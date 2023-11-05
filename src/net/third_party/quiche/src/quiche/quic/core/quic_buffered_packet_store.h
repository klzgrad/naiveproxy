// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_
#define QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_

#include <list>
#include <string>

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/tls_chlo_extractor.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
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
    TOO_MANY_PACKETS,  // Too many packets stored up for a certain connection.
    TOO_MANY_CONNECTIONS  // Too many connections stored up in the store.
  };

  struct QUICHE_EXPORT BufferedPacket {
    BufferedPacket(std::unique_ptr<QuicReceivedPacket> packet,
                   QuicSocketAddress self_address,
                   QuicSocketAddress peer_address);
    BufferedPacket(BufferedPacket&& other);

    BufferedPacket& operator=(BufferedPacket&& other);

    ~BufferedPacket();

    std::unique_ptr<QuicReceivedPacket> packet;
    QuicSocketAddress self_address;
    QuicSocketAddress peer_address;
  };

  // A queue of BufferedPackets for a connection.
  struct QUICHE_EXPORT BufferedPacketList {
    BufferedPacketList();
    BufferedPacketList(BufferedPacketList&& other);

    BufferedPacketList& operator=(BufferedPacketList&& other);

    ~BufferedPacketList();

    std::list<BufferedPacket> buffered_packets;
    QuicTime creation_time;
    // |parsed_chlo| is set iff the entire CHLO has been received.
    absl::optional<ParsedClientHello> parsed_chlo;
    // Indicating whether this is an IETF QUIC connection.
    bool ietf_quic;
    // If buffered_packets contains the CHLO, it is the version of the CHLO.
    // Otherwise, it is the version of the first packet in |buffered_packets|.
    ParsedQuicVersion version;
    TlsChloExtractor tls_chlo_extractor;
  };

  using BufferedPacketMap =
      quiche::QuicheLinkedHashMap<QuicConnectionId, BufferedPacketList,
                                  QuicConnectionIdHash>;

  class QUICHE_EXPORT VisitorInterface {
   public:
    virtual ~VisitorInterface() {}

    // Called for each expired connection when alarm fires.
    virtual void OnExpiredPackets(QuicConnectionId connection_id,
                                  BufferedPacketList early_arrived_packets) = 0;
  };

  QuicBufferedPacketStore(VisitorInterface* visitor, const QuicClock* clock,
                          QuicAlarmFactory* alarm_factory);

  QuicBufferedPacketStore(const QuicBufferedPacketStore&) = delete;

  ~QuicBufferedPacketStore();

  QuicBufferedPacketStore& operator=(const QuicBufferedPacketStore&) = delete;

  // Adds a copy of packet into the packet queue for given connection. If the
  // packet is the last one of the CHLO, |parsed_chlo| will contain a parsed
  // version of the CHLO.
  EnqueuePacketResult EnqueuePacket(
      QuicConnectionId connection_id, bool ietf_quic,
      const QuicReceivedPacket& packet, QuicSocketAddress self_address,
      QuicSocketAddress peer_address, const ParsedQuicVersion& version,
      absl::optional<ParsedClientHello> parsed_chlo);

  // Returns true if there are any packets buffered for |connection_id|.
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
      std::vector<std::string>* out_alpns, std::string* out_sni,
      bool* out_resumption_attempted, bool* out_early_data_attempted,
      absl::optional<uint8_t>* tls_alert);

  // Returns the list of buffered packets for |connection_id| and removes them
  // from the store. Returns an empty list if no early arrived packets for this
  // connection are present.
  BufferedPacketList DeliverPackets(QuicConnectionId connection_id);

  // Discards packets buffered for |connection_id|, if any.
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

  // Is given connection already buffered in the store?
  bool HasChloForConnection(QuicConnectionId connection_id);

  // Is there any CHLO buffered in the store?
  bool HasChlosBuffered() const;

 private:
  friend class test::QuicBufferedPacketStorePeer;

  // Set expiration alarm if it hasn't been set.
  void MaybeSetExpirationAlarm();

  // Return true if add an extra packet will go beyond allowed max connection
  // limit. The limit for non-CHLO packet and CHLO packet is different.
  bool ShouldNotBufferPacket(bool is_chlo);

  // A map to store packet queues with creation time for each connection.
  BufferedPacketMap undecryptable_packets_;

  // The max time the packets of a connection can be buffer in the store.
  const QuicTime::Delta connection_life_span_;

  VisitorInterface* visitor_;  // Unowned.

  const QuicClock* clock_;  // Unowned.

  // This alarm fires every |connection_life_span_| to clean up
  // packets staying in the store for too long.
  std::unique_ptr<QuicAlarm> expiration_alarm_;

  // Keeps track of connection with CHLO buffered up already and the order they
  // arrive.
  quiche::QuicheLinkedHashMap<QuicConnectionId, bool, QuicConnectionIdHash>
      connections_with_chlo_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_
