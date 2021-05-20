// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_
#define QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_

#include <list>
#include <string>

#include "quic/core/quic_alarm.h"
#include "quic/core/quic_alarm_factory.h"
#include "quic/core/quic_clock.h"
#include "quic/core/quic_packets.h"
#include "quic/core/quic_time.h"
#include "quic/core/tls_chlo_extractor.h"
#include "quic/platform/api/quic_containers.h"
#include "quic/platform/api/quic_export.h"
#include "quic/platform/api/quic_socket_address.h"

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
class QUIC_NO_EXPORT QuicBufferedPacketStore {
 public:
  enum EnqueuePacketResult {
    SUCCESS = 0,
    TOO_MANY_PACKETS,  // Too many packets stored up for a certain connection.
    TOO_MANY_CONNECTIONS  // Too many connections stored up in the store.
  };

  struct QUIC_NO_EXPORT BufferedPacket {
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
  struct QUIC_NO_EXPORT BufferedPacketList {
    BufferedPacketList();
    BufferedPacketList(BufferedPacketList&& other);

    BufferedPacketList& operator=(BufferedPacketList&& other);

    ~BufferedPacketList();

    std::list<BufferedPacket> buffered_packets;
    QuicTime creation_time;
    // The ALPNs from the CHLO, if found.
    std::vector<std::string> alpns;
    // Indicating whether this is an IETF QUIC connection.
    bool ietf_quic;
    // If buffered_packets contains the CHLO, it is the version of the CHLO.
    // Otherwise, it is the version of the first packet in |buffered_packets|.
    ParsedQuicVersion version;
    TlsChloExtractor tls_chlo_extractor;
  };

  using BufferedPacketMap = QuicLinkedHashMap<QuicConnectionId,
                                              BufferedPacketList,
                                              QuicConnectionIdHash>;

  class QUIC_NO_EXPORT VisitorInterface {
   public:
    virtual ~VisitorInterface() {}

    // Called for each expired connection when alarm fires.
    virtual void OnExpiredPackets(QuicConnectionId connection_id,
                                  BufferedPacketList early_arrived_packets) = 0;
  };

  QuicBufferedPacketStore(VisitorInterface* vistor,
                          const QuicClock* clock,
                          QuicAlarmFactory* alarm_factory);

  QuicBufferedPacketStore(const QuicBufferedPacketStore&) = delete;

  ~QuicBufferedPacketStore();

  QuicBufferedPacketStore& operator=(const QuicBufferedPacketStore&) = delete;

  // Adds a copy of packet into packet queue for given connection.
  // TODO(danzh): Consider to split this method to EnqueueChlo() and
  // EnqueueDataPacket().
  EnqueuePacketResult EnqueuePacket(QuicConnectionId connection_id,
                                    bool ietf_quic,
                                    const QuicReceivedPacket& packet,
                                    QuicSocketAddress self_address,
                                    QuicSocketAddress peer_address,
                                    bool is_chlo,
                                    const std::vector<std::string>& alpns,
                                    const ParsedQuicVersion& version);

  // Returns true if there are any packets buffered for |connection_id|.
  bool HasBufferedPackets(QuicConnectionId connection_id) const;

  // Ingests this packet into the corresponding TlsChloExtractor. This should
  // only be called when HasBufferedPackets(connection_id) is true.
  // Returns whether we've now parsed a full multi-packet TLS CHLO.
  // When this returns true, |out_alpns| is populated with the list of ALPNs
  // extracted from the CHLO.
  bool IngestPacketForTlsChloExtraction(const QuicConnectionId& connection_id,
                                        const ParsedQuicVersion& version,
                                        const QuicReceivedPacket& packet,
                                        std::vector<std::string>* out_alpns);

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
  QuicLinkedHashMap<QuicConnectionId, bool, QuicConnectionIdHash>
      connections_with_chlo_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_BUFFERED_PACKET_STORE_H_
