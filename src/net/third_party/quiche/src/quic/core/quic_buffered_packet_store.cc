// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_buffered_packet_store.h"

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"

namespace quic {

typedef QuicBufferedPacketStore::BufferedPacket BufferedPacket;
typedef QuicBufferedPacketStore::BufferedPacketList BufferedPacketList;
typedef QuicBufferedPacketStore::EnqueuePacketResult EnqueuePacketResult;

// Max number of connections this store can keep track.
static const size_t kDefaultMaxConnectionsInStore = 100;
// Up to half of the capacity can be used for storing non-CHLO packets.
static const size_t kMaxConnectionsWithoutCHLO =
    kDefaultMaxConnectionsInStore / 2;

namespace {

// This alarm removes expired entries in map each time this alarm fires.
class ConnectionExpireAlarm : public QuicAlarm::Delegate {
 public:
  explicit ConnectionExpireAlarm(QuicBufferedPacketStore* store)
      : connection_store_(store) {}

  void OnAlarm() override { connection_store_->OnExpirationTimeout(); }

  ConnectionExpireAlarm(const ConnectionExpireAlarm&) = delete;
  ConnectionExpireAlarm& operator=(const ConnectionExpireAlarm&) = delete;

 private:
  QuicBufferedPacketStore* connection_store_;
};

}  // namespace

BufferedPacket::BufferedPacket(std::unique_ptr<QuicReceivedPacket> packet,
                               QuicSocketAddress self_address,
                               QuicSocketAddress peer_address)
    : packet(std::move(packet)),
      self_address(self_address),
      peer_address(peer_address) {}

BufferedPacket::BufferedPacket(BufferedPacket&& other) = default;

BufferedPacket& BufferedPacket::operator=(BufferedPacket&& other) = default;

BufferedPacket::~BufferedPacket() {}

BufferedPacketList::BufferedPacketList()
    : creation_time(QuicTime::Zero()),
      ietf_quic(false),
      version(PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED) {}

BufferedPacketList::BufferedPacketList(BufferedPacketList&& other) = default;

BufferedPacketList& BufferedPacketList::operator=(BufferedPacketList&& other) =
    default;

BufferedPacketList::~BufferedPacketList() {}

QuicBufferedPacketStore::QuicBufferedPacketStore(
    VisitorInterface* visitor,
    const QuicClock* clock,
    QuicAlarmFactory* alarm_factory)
    : connection_life_span_(
          QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs)),
      visitor_(visitor),
      clock_(clock),
      expiration_alarm_(
          alarm_factory->CreateAlarm(new ConnectionExpireAlarm(this))) {}

QuicBufferedPacketStore::~QuicBufferedPacketStore() {}

EnqueuePacketResult QuicBufferedPacketStore::EnqueuePacket(
    QuicConnectionId connection_id,
    bool ietf_quic,
    const QuicReceivedPacket& packet,
    QuicSocketAddress self_address,
    QuicSocketAddress peer_address,
    bool is_chlo,
    const std::string& alpn,
    const ParsedQuicVersion& version) {
  QUIC_BUG_IF(!GetQuicFlag(FLAGS_quic_allow_chlo_buffering))
      << "Shouldn't buffer packets if disabled via flag.";
  QUIC_BUG_IF(is_chlo && QuicContainsKey(connections_with_chlo_, connection_id))
      << "Shouldn't buffer duplicated CHLO on connection " << connection_id;
  QUIC_BUG_IF(!is_chlo && !alpn.empty())
      << "Shouldn't have an ALPN defined for a non-CHLO packet.";
  QUIC_BUG_IF(is_chlo && version.transport_version == QUIC_VERSION_UNSUPPORTED)
      << "Should have version for CHLO packet.";

  if (!QuicContainsKey(undecryptable_packets_, connection_id) &&
      ShouldBufferPacket(is_chlo)) {
    // Drop the packet if the upper limit of undecryptable packets has been
    // reached or the whole capacity of the store has been reached.
    return TOO_MANY_CONNECTIONS;
  } else if (!QuicContainsKey(undecryptable_packets_, connection_id)) {
    undecryptable_packets_.emplace(
        std::make_pair(connection_id, BufferedPacketList()));
    undecryptable_packets_.back().second.ietf_quic = ietf_quic;
    undecryptable_packets_.back().second.version = version;
  }
  CHECK(QuicContainsKey(undecryptable_packets_, connection_id));
  BufferedPacketList& queue =
      undecryptable_packets_.find(connection_id)->second;

  if (!is_chlo) {
    // If current packet is not CHLO, it might not be buffered because store
    // only buffers certain number of undecryptable packets per connection.
    size_t num_non_chlo_packets =
        QuicContainsKey(connections_with_chlo_, connection_id)
            ? (queue.buffered_packets.size() - 1)
            : queue.buffered_packets.size();
    if (num_non_chlo_packets >= kDefaultMaxUndecryptablePackets) {
      // If there are kMaxBufferedPacketsPerConnection packets buffered up for
      // this connection, drop the current packet.
      return TOO_MANY_PACKETS;
    }
  }

  if (queue.buffered_packets.empty()) {
    // If this is the first packet arrived on a new connection, initialize the
    // creation time.
    queue.creation_time = clock_->ApproximateNow();
  }

  BufferedPacket new_entry(std::unique_ptr<QuicReceivedPacket>(packet.Clone()),
                           self_address, peer_address);
  if (is_chlo) {
    // Add CHLO to the beginning of buffered packets so that it can be delivered
    // first later.
    queue.buffered_packets.push_front(std::move(new_entry));
    queue.alpn = alpn;
    connections_with_chlo_[connection_id] = false;  // Dummy value.
    // Set the version of buffered packets of this connection on CHLO.
    queue.version = version;
  } else {
    // Buffer non-CHLO packets in arrival order.
    queue.buffered_packets.push_back(std::move(new_entry));
  }
  MaybeSetExpirationAlarm();
  return SUCCESS;
}

bool QuicBufferedPacketStore::HasBufferedPackets(
    QuicConnectionId connection_id) const {
  return QuicContainsKey(undecryptable_packets_, connection_id);
}

bool QuicBufferedPacketStore::HasChlosBuffered() const {
  return !connections_with_chlo_.empty();
}

BufferedPacketList QuicBufferedPacketStore::DeliverPackets(
    QuicConnectionId connection_id) {
  BufferedPacketList packets_to_deliver;
  auto it = undecryptable_packets_.find(connection_id);
  if (it != undecryptable_packets_.end()) {
    packets_to_deliver = std::move(it->second);
    undecryptable_packets_.erase(connection_id);
  }
  return packets_to_deliver;
}

void QuicBufferedPacketStore::DiscardPackets(QuicConnectionId connection_id) {
  undecryptable_packets_.erase(connection_id);
  connections_with_chlo_.erase(connection_id);
}

void QuicBufferedPacketStore::DiscardAllPackets() {
  undecryptable_packets_.clear();
  connections_with_chlo_.clear();
  expiration_alarm_->Cancel();
}

void QuicBufferedPacketStore::OnExpirationTimeout() {
  QuicTime expiration_time = clock_->ApproximateNow() - connection_life_span_;
  while (!undecryptable_packets_.empty()) {
    auto& entry = undecryptable_packets_.front();
    if (entry.second.creation_time > expiration_time) {
      break;
    }
    QuicConnectionId connection_id = entry.first;
    visitor_->OnExpiredPackets(connection_id, std::move(entry.second));
    undecryptable_packets_.pop_front();
    connections_with_chlo_.erase(connection_id);
  }
  if (!undecryptable_packets_.empty()) {
    MaybeSetExpirationAlarm();
  }
}

void QuicBufferedPacketStore::MaybeSetExpirationAlarm() {
  if (!expiration_alarm_->IsSet()) {
    expiration_alarm_->Set(clock_->ApproximateNow() + connection_life_span_);
  }
}

bool QuicBufferedPacketStore::ShouldBufferPacket(bool is_chlo) {
  bool is_store_full =
      undecryptable_packets_.size() >= kDefaultMaxConnectionsInStore;

  if (is_chlo) {
    return is_store_full;
  }

  size_t num_connections_without_chlo =
      undecryptable_packets_.size() - connections_with_chlo_.size();
  bool reach_non_chlo_limit =
      num_connections_without_chlo >= kMaxConnectionsWithoutCHLO;

  return is_store_full || reach_non_chlo_limit;
}

BufferedPacketList QuicBufferedPacketStore::DeliverPacketsForNextConnection(
    QuicConnectionId* connection_id) {
  if (connections_with_chlo_.empty()) {
    // Returns empty list if no CHLO has been buffered.
    return BufferedPacketList();
  }
  *connection_id = connections_with_chlo_.front().first;
  connections_with_chlo_.pop_front();

  BufferedPacketList packets = DeliverPackets(*connection_id);
  DCHECK(!packets.buffered_packets.empty())
      << "Try to deliver connectons without CHLO";
  return packets;
}

bool QuicBufferedPacketStore::HasChloForConnection(
    QuicConnectionId connection_id) {
  return QuicContainsKey(connections_with_chlo_, connection_id);
}

}  // namespace quic
