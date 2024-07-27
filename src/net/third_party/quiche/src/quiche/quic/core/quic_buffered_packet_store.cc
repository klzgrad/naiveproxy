// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_buffered_packet_store.h"

#include <cstddef>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

using BufferedPacket = QuicBufferedPacketStore::BufferedPacket;
using BufferedPacketList = QuicBufferedPacketStore::BufferedPacketList;
using EnqueuePacketResult = QuicBufferedPacketStore::EnqueuePacketResult;

// Max number of connections this store can keep track.
static const size_t kDefaultMaxConnectionsInStore = 100;
// Up to half of the capacity can be used for storing non-CHLO packets.
static const size_t kMaxConnectionsWithoutCHLO =
    kDefaultMaxConnectionsInStore / 2;

namespace {

// This alarm removes expired entries in map each time this alarm fires.
class ConnectionExpireAlarm : public QuicAlarm::DelegateWithoutContext {
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
      version(ParsedQuicVersion::Unsupported()) {}

BufferedPacketList::BufferedPacketList(BufferedPacketList&& other) = default;

BufferedPacketList& BufferedPacketList::operator=(BufferedPacketList&& other) =
    default;

BufferedPacketList::~BufferedPacketList() {}

QuicBufferedPacketStore::QuicBufferedPacketStore(
    VisitorInterface* visitor, const QuicClock* clock,
    QuicAlarmFactory* alarm_factory)
    : connection_life_span_(
          QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs)),
      visitor_(visitor),
      clock_(clock),
      expiration_alarm_(
          alarm_factory->CreateAlarm(new ConnectionExpireAlarm(this))) {}

QuicBufferedPacketStore::~QuicBufferedPacketStore() {
  if (expiration_alarm_ != nullptr) {
    expiration_alarm_->PermanentCancel();
  }
}

EnqueuePacketResult QuicBufferedPacketStore::EnqueuePacket(
    QuicConnectionId connection_id, bool ietf_quic,
    const QuicReceivedPacket& packet, QuicSocketAddress self_address,
    QuicSocketAddress peer_address, const ParsedQuicVersion& version,
    std::optional<ParsedClientHello> parsed_chlo,
    ConnectionIdGeneratorInterface* connection_id_generator) {
  const bool is_chlo = parsed_chlo.has_value();
  QUIC_BUG_IF(quic_bug_12410_1, !GetQuicFlag(quic_allow_chlo_buffering))
      << "Shouldn't buffer packets if disabled via flag.";
  QUIC_BUG_IF(quic_bug_12410_2,
              is_chlo && connections_with_chlo_.contains(connection_id))
      << "Shouldn't buffer duplicated CHLO on connection " << connection_id;
  QUIC_BUG_IF(quic_bug_12410_4, is_chlo && !version.IsKnown())
      << "Should have version for CHLO packet.";

  const bool is_first_packet = !undecryptable_packets_.contains(connection_id);
  if (is_first_packet) {
    if (ShouldNotBufferPacket(is_chlo)) {
      // Drop the packet if the upper limit of undecryptable packets has been
      // reached or the whole capacity of the store has been reached.
      return TOO_MANY_CONNECTIONS;
    }
    undecryptable_packets_.emplace(
        std::make_pair(connection_id, BufferedPacketList()));
    undecryptable_packets_.back().second.ietf_quic = ietf_quic;
    undecryptable_packets_.back().second.version = version;
  }
  QUICHE_CHECK(undecryptable_packets_.contains(connection_id));
  BufferedPacketList& queue =
      undecryptable_packets_.find(connection_id)->second;

  if (!is_chlo) {
    // If current packet is not CHLO, it might not be buffered because store
    // only buffers certain number of undecryptable packets per connection.
    size_t num_non_chlo_packets = connections_with_chlo_.contains(connection_id)
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
    queue.parsed_chlo = std::move(parsed_chlo);
    connections_with_chlo_[connection_id] = false;  // Dummy value.
    // Set the version of buffered packets of this connection on CHLO.
    queue.version = version;
    queue.connection_id_generator = connection_id_generator;
  } else {
    // Buffer non-CHLO packets in arrival order.
    queue.buffered_packets.push_back(std::move(new_entry));

    // Attempt to parse multi-packet TLS CHLOs.
    if (is_first_packet) {
      queue.tls_chlo_extractor.IngestPacket(version, packet);
      // Since this is the first packet and it's not a CHLO, the
      // TlsChloExtractor should not have the entire CHLO.
      QUIC_BUG_IF(quic_bug_12410_5,
                  queue.tls_chlo_extractor.HasParsedFullChlo())
          << "First packet in list should not contain full CHLO";
    }
    // TODO(b/154857081) Reorder CHLO packets ahead of other ones.
  }

  MaybeSetExpirationAlarm();
  return SUCCESS;
}

bool QuicBufferedPacketStore::HasBufferedPackets(
    QuicConnectionId connection_id) const {
  return undecryptable_packets_.contains(connection_id);
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
    std::list<BufferedPacket> initial_packets;
    std::list<BufferedPacket> other_packets;
    for (auto& packet : packets_to_deliver.buffered_packets) {
      QuicLongHeaderType long_packet_type = INVALID_PACKET_TYPE;
      PacketHeaderFormat unused_format;
      bool unused_version_flag;
      bool unused_use_length_prefix;
      QuicVersionLabel unused_version_label;
      ParsedQuicVersion unused_parsed_version = UnsupportedQuicVersion();
      QuicConnectionId unused_destination_connection_id;
      QuicConnectionId unused_source_connection_id;
      std::optional<absl::string_view> unused_retry_token;
      std::string unused_detailed_error;

      // We don't need to pass |generator| because we already got the correct
      // connection ID length when we buffered the packet and indexed by
      // connection ID.
      QuicErrorCode error_code = QuicFramer::ParsePublicHeaderDispatcher(
          *packet.packet, connection_id.length(), &unused_format,
          &long_packet_type, &unused_version_flag, &unused_use_length_prefix,
          &unused_version_label, &unused_parsed_version,
          &unused_destination_connection_id, &unused_source_connection_id,
          &unused_retry_token, &unused_detailed_error);

      if (error_code == QUIC_NO_ERROR && long_packet_type == INITIAL) {
        initial_packets.push_back(std::move(packet));
      } else {
        other_packets.push_back(std::move(packet));
      }
    }

    initial_packets.splice(initial_packets.end(), other_packets);
    packets_to_deliver.buffered_packets = std::move(initial_packets);
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

bool QuicBufferedPacketStore::ShouldNotBufferPacket(bool is_chlo) {
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
  QUICHE_DCHECK(!packets.buffered_packets.empty() &&
                packets.parsed_chlo.has_value())
      << "Try to deliver connectons without CHLO. # packets:"
      << packets.buffered_packets.size()
      << ", has_parsed_chlo:" << packets.parsed_chlo.has_value();
  return packets;
}

bool QuicBufferedPacketStore::HasChloForConnection(
    QuicConnectionId connection_id) {
  return connections_with_chlo_.contains(connection_id);
}

bool QuicBufferedPacketStore::IngestPacketForTlsChloExtraction(
    const QuicConnectionId& connection_id, const ParsedQuicVersion& version,
    const QuicReceivedPacket& packet,
    std::vector<uint16_t>* out_supported_groups,
    std::vector<std::string>* out_alpns, std::string* out_sni,
    bool* out_resumption_attempted, bool* out_early_data_attempted,
    std::optional<uint8_t>* tls_alert) {
  QUICHE_DCHECK_NE(out_alpns, nullptr);
  QUICHE_DCHECK_NE(out_sni, nullptr);
  QUICHE_DCHECK_NE(tls_alert, nullptr);
  QUICHE_DCHECK_EQ(version.handshake_protocol, PROTOCOL_TLS1_3);
  auto it = undecryptable_packets_.find(connection_id);
  if (it == undecryptable_packets_.end()) {
    QUIC_BUG(quic_bug_10838_1)
        << "Cannot ingest packet for unknown connection ID " << connection_id;
    return false;
  }
  it->second.tls_chlo_extractor.IngestPacket(version, packet);
  if (!it->second.tls_chlo_extractor.HasParsedFullChlo()) {
    *tls_alert = it->second.tls_chlo_extractor.tls_alert();
    return false;
  }
  const TlsChloExtractor& tls_chlo_extractor = it->second.tls_chlo_extractor;
  *out_supported_groups = tls_chlo_extractor.supported_groups();
  *out_alpns = tls_chlo_extractor.alpns();
  *out_sni = tls_chlo_extractor.server_name();
  *out_resumption_attempted = tls_chlo_extractor.resumption_attempted();
  *out_early_data_attempted = tls_chlo_extractor.early_data_attempted();
  return true;
}

}  // namespace quic
