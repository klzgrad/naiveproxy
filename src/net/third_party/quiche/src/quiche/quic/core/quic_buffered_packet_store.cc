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

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_packet_number.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/print_elements.h"
#include "quiche/common/simple_buffer_allocator.h"

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
                               QuicSocketAddress peer_address,
                               bool is_ietf_initial_packet)
    : packet(std::move(packet)),
      self_address(self_address),
      peer_address(peer_address),
      is_ietf_initial_packet(is_ietf_initial_packet) {}

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
    QuicAlarmFactory* alarm_factory, QuicDispatcherStats& stats)
    : stats_(stats),
      connection_life_span_(
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
    const ReceivedPacketInfo& packet_info,
    std::optional<ParsedClientHello> parsed_chlo,
    ConnectionIdGeneratorInterface& connection_id_generator) {
  QuicConnectionId connection_id = packet_info.destination_connection_id;
  const QuicReceivedPacket& packet = packet_info.packet;
  const QuicSocketAddress& self_address = packet_info.self_address;
  const QuicSocketAddress& peer_address = packet_info.peer_address;
  const ParsedQuicVersion& version = packet_info.version;
  const bool ietf_quic = packet_info.form != GOOGLE_QUIC_PACKET;
  const bool is_chlo = parsed_chlo.has_value();
  const bool is_ietf_initial_packet =
      (version.IsKnown() && packet_info.form == IETF_QUIC_LONG_HEADER_PACKET &&
       packet_info.long_packet_type == INITIAL);
  QUIC_BUG_IF(quic_bug_12410_1, !GetQuicFlag(quic_allow_chlo_buffering))
      << "Shouldn't buffer packets if disabled via flag.";
  QUIC_BUG_IF(quic_bug_12410_4, is_chlo && !version.IsKnown())
      << "Should have version for CHLO packet.";

  auto iter = buffered_session_map_.find(connection_id);
  const bool is_first_packet = (iter == buffered_session_map_.end());
  if (is_first_packet) {
    if (ShouldNotBufferPacket(is_chlo)) {
      // Drop the packet if the upper limit of undecryptable packets has been
      // reached or the whole capacity of the store has been reached.
      return TOO_MANY_CONNECTIONS;
    }
    iter = buffered_session_map_.emplace_hint(
        iter, connection_id, std::make_shared<BufferedPacketListNode>());
    iter->second->ietf_quic = ietf_quic;
    iter->second->version = version;
    iter->second->original_connection_id = connection_id;
    iter->second->creation_time = clock_->ApproximateNow();
    buffered_sessions_.push_back(iter->second.get());
    ++num_buffered_sessions_;
  }
  QUICHE_DCHECK(buffered_session_map_.contains(connection_id));

  BufferedPacketListNode& queue = *iter->second;

  // TODO(wub): Rename kDefaultMaxUndecryptablePackets to kMaxBufferedPackets.
  if (!is_chlo &&
      queue.buffered_packets.size() >= kDefaultMaxUndecryptablePackets) {
    // If there are kMaxBufferedPacketsPerConnection packets buffered up for
    // this connection, drop the current packet.
    return TOO_MANY_PACKETS;
  }

  BufferedPacket new_entry(std::unique_ptr<QuicReceivedPacket>(packet.Clone()),
                           self_address, peer_address, is_ietf_initial_packet);
  if (is_chlo) {
    // Add CHLO to the beginning of buffered packets so that it can be delivered
    // first later.
    queue.buffered_packets.push_front(std::move(new_entry));
    queue.parsed_chlo = std::move(parsed_chlo);
    // Set the version of buffered packets of this connection on CHLO.
    queue.version = version;
    if (!buffered_sessions_with_chlo_.is_linked(&queue)) {
      buffered_sessions_with_chlo_.push_back(&queue);
      ++num_buffered_sessions_with_chlo_;
    } else {
      QUIC_BUG(quic_store_session_already_has_chlo)
          << "Buffered session already has CHLO";
    }
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

  if (is_ietf_initial_packet && version.UsesTls() &&
      !queue.HasAttemptedToReplaceConnectionId()) {
    queue.SetAttemptedToReplaceConnectionId(&connection_id_generator);
    std::optional<QuicConnectionId> replaced_connection_id =
        connection_id_generator.MaybeReplaceConnectionId(connection_id,
                                                         packet_info.version);
    // Normalize the output of MaybeReplaceConnectionId.
    if (replaced_connection_id.has_value() &&
        (replaced_connection_id->IsEmpty() ||
         *replaced_connection_id == connection_id)) {
      QUIC_CODE_COUNT(quic_store_replaced_cid_is_empty_or_same_as_original);
      replaced_connection_id.reset();
    }
    QUIC_DVLOG(1) << "MaybeReplaceConnectionId(" << connection_id << ") = "
                  << (replaced_connection_id.has_value()
                          ? replaced_connection_id->ToString()
                          : "nullopt");
    if (replaced_connection_id.has_value()) {
      switch (visitor_->HandleConnectionIdCollision(
          connection_id, *replaced_connection_id, self_address, peer_address,
          version,
          queue.parsed_chlo.has_value() ? &queue.parsed_chlo.value()
                                        : nullptr)) {
        case VisitorInterface::HandleCidCollisionResult::kOk:
          queue.replaced_connection_id = *replaced_connection_id;
          buffered_session_map_.insert(
              {*replaced_connection_id, queue.shared_from_this()});
          break;
        case VisitorInterface::HandleCidCollisionResult::kCollision:
          return CID_COLLISION;
      }
    }
  }

  MaybeAckInitialPacket(packet_info, queue);
  if (is_chlo) {
    ++stats_.packets_enqueued_chlo;
  } else {
    ++stats_.packets_enqueued_early;
  }
  return SUCCESS;
}

void QuicBufferedPacketStore::MaybeAckInitialPacket(
    const ReceivedPacketInfo& packet_info, BufferedPacketList& packet_list) {
  if (!ack_buffered_initial_packets_) {
    return;
  }

  QUIC_RESTART_FLAG_COUNT_N(quic_dispatcher_ack_buffered_initial_packets, 1, 8);
  if (writer_ == nullptr || writer_->IsWriteBlocked() ||
      !packet_info.version.IsKnown() ||
      !packet_list.HasAttemptedToReplaceConnectionId() ||
      // Do not ack initial packet if entire CHLO is buffered.
      packet_list.parsed_chlo.has_value() ||
      packet_list.dispatcher_sent_packets.size() >=
          GetQuicFlag(quic_dispatcher_max_ack_sent_per_connection)) {
    return;
  }

  absl::InlinedVector<DispatcherSentPacket, 2>& dispatcher_sent_packets =
      packet_list.dispatcher_sent_packets;
  const QuicConnectionId& original_connection_id =
      packet_list.original_connection_id;

  CrypterPair crypters;
  CryptoUtils::CreateInitialObfuscators(Perspective::IS_SERVER,
                                        packet_info.version,
                                        original_connection_id, &crypters);
  QuicPacketNumber prior_largest_acked;
  if (!dispatcher_sent_packets.empty()) {
    prior_largest_acked = dispatcher_sent_packets.back().largest_acked;
  }

  std::optional<uint64_t> packet_number;
  if (!(QUIC_NO_ERROR == QuicFramer::TryDecryptInitialPacketDispatcher(
                             packet_info.packet, packet_info.version,
                             packet_info.form, packet_info.long_packet_type,
                             packet_info.destination_connection_id,
                             packet_info.source_connection_id,
                             packet_info.retry_token, prior_largest_acked,
                             *crypters.decrypter, &packet_number) &&
        packet_number.has_value())) {
    QUIC_CODE_COUNT(quic_store_failed_to_decrypt_initial_packet);
    QUIC_DVLOG(1) << "Failed to decrypt initial packet. "
                     "packet_info.destination_connection_id:"
                  << packet_info.destination_connection_id
                  << ", original_connection_id: " << original_connection_id
                  << ", replaced_connection_id: "
                  << (packet_list.HasReplacedConnectionId()
                          ? packet_list.replaced_connection_id->ToString()
                          : "n/a");
    return;
  }

  const QuicConnectionId& server_connection_id =
      packet_list.HasReplacedConnectionId()
          ? *packet_list.replaced_connection_id
          : original_connection_id;
  QuicFramer framer(ParsedQuicVersionVector{packet_info.version},
                    /*unused*/ QuicTime::Zero(), Perspective::IS_SERVER,
                    /*unused*/ server_connection_id.length());
  framer.SetInitialObfuscators(original_connection_id);

  quiche::SimpleBufferAllocator send_buffer_allocator;
  PacketCollector collector(&send_buffer_allocator);
  QuicPacketCreator creator(server_connection_id, &framer, &collector);

  if (!dispatcher_sent_packets.empty()) {
    // Sets the *last sent* packet number, creator will derive the next sending
    // packet number accordingly.
    creator.set_packet_number(dispatcher_sent_packets.back().packet_number);
  }

  QuicAckFrame initial_ack_frame;
  initial_ack_frame.ack_delay_time = QuicTimeDelta::Zero();
  initial_ack_frame.packets.Add(QuicPacketNumber(*packet_number));
  for (const DispatcherSentPacket& sent_packet : dispatcher_sent_packets) {
    initial_ack_frame.packets.Add(sent_packet.received_packet_number);
  }
  initial_ack_frame.largest_acked = initial_ack_frame.packets.Max();

  if (!creator.AddFrame(QuicFrame(&initial_ack_frame), NOT_RETRANSMISSION)) {
    QUIC_BUG(quic_dispatcher_add_ack_frame_failed)
        << "Unable to add ack frame to an empty packet while acking packet "
        << *packet_number;
    return;
  }
  creator.FlushCurrentPacket();
  if (collector.packets()->size() != 1) {
    QUIC_BUG(quic_dispatcher_ack_unexpected_packet_count)
        << "Expect 1 ack packet created, got " << collector.packets()->size();
    return;
  }

  std::unique_ptr<QuicEncryptedPacket>& packet = collector.packets()->front();

  // For easy grep'ing, use a similar logging format as the log in
  // QuicConnection::WritePacket.
  QUIC_DVLOG(1) << "Server: Sending packet " << creator.packet_number()
                << " : ack only from dispatcher, encryption_level: "
                   "ENCRYPTION_INITIAL, encrypted length: "
                << packet->length() << " to peer " << packet_info.peer_address
                << ". packet_info.destination_connection_id: "
                << packet_info.destination_connection_id
                << ", original_connection_id: " << original_connection_id
                << ", replaced_connection_id: "
                << (packet_list.HasReplacedConnectionId()
                        ? packet_list.replaced_connection_id->ToString()
                        : "n/a");

  WriteResult result = writer_->WritePacket(
      packet->data(), packet->length(), packet_info.self_address.host(),
      packet_info.peer_address, nullptr, QuicPacketWriterParams());
  writer_->Flush();
  QUIC_HISTOGRAM_ENUM("QuicBufferedPacketStore.WritePacketStatus",
                      result.status, WRITE_STATUS_NUM_VALUES,
                      "Status code returned by writer_->WritePacket() in "
                      "QuicBufferedPacketStore.");

  DispatcherSentPacket sent_packet;
  sent_packet.packet_number = creator.packet_number();
  sent_packet.received_packet_number = QuicPacketNumber(*packet_number);
  sent_packet.largest_acked = initial_ack_frame.largest_acked;
  sent_packet.sent_time = clock_->ApproximateNow();
  sent_packet.bytes_sent = static_cast<QuicPacketLength>(packet->length());

  dispatcher_sent_packets.push_back(sent_packet);
  ++stats_.packets_sent;
}

bool QuicBufferedPacketStore::HasBufferedPackets(
    QuicConnectionId connection_id) const {
  return buffered_session_map_.contains(connection_id);
}

bool QuicBufferedPacketStore::HasChlosBuffered() const {
  return num_buffered_sessions_with_chlo_ != 0;
}

const BufferedPacketList* QuicBufferedPacketStore::GetPacketList(
    const QuicConnectionId& connection_id) const {
  if (!ack_buffered_initial_packets_) {
    return nullptr;
  }

  QUIC_RESTART_FLAG_COUNT_N(quic_dispatcher_ack_buffered_initial_packets, 2, 8);
  auto it = buffered_session_map_.find(connection_id);
  if (it == buffered_session_map_.end()) {
    return nullptr;
  }
  QUICHE_DCHECK(CheckInvariants(*it->second));
  return it->second.get();
}

bool QuicBufferedPacketStore::CheckInvariants(
    const BufferedPacketList& packet_list) const {
  auto original_cid_it =
      buffered_session_map_.find(packet_list.original_connection_id);
  if (original_cid_it == buffered_session_map_.end()) {
    return false;
  }
  if (original_cid_it->second.get() != &packet_list) {
    return false;
  }
  if (buffered_sessions_with_chlo_.is_linked(original_cid_it->second.get()) !=
      original_cid_it->second->parsed_chlo.has_value()) {
    return false;
  }
  if (packet_list.replaced_connection_id.has_value()) {
    auto replaced_cid_it =
        buffered_session_map_.find(*packet_list.replaced_connection_id);
    if (replaced_cid_it == buffered_session_map_.end()) {
      return false;
    }
    if (replaced_cid_it->second.get() != &packet_list) {
      return false;
    }
  }

  return true;
}

BufferedPacketList QuicBufferedPacketStore::DeliverPackets(
    QuicConnectionId connection_id) {
  auto it = buffered_session_map_.find(connection_id);
  if (it == buffered_session_map_.end()) {
    return BufferedPacketList();
  }

  std::shared_ptr<BufferedPacketListNode> node = it->second->shared_from_this();
  RemoveFromStore(*node);
  std::list<BufferedPacket> initial_packets;
  std::list<BufferedPacket> other_packets;
  for (auto& packet : node->buffered_packets) {
    if (packet.is_ietf_initial_packet) {
      initial_packets.push_back(std::move(packet));
    } else {
      other_packets.push_back(std::move(packet));
    }
  }
  initial_packets.splice(initial_packets.end(), other_packets);
  node->buffered_packets = std::move(initial_packets);
  BufferedPacketList& packet_list = *node;
  return std::move(packet_list);
}

void QuicBufferedPacketStore::DiscardPackets(QuicConnectionId connection_id) {
  auto it = buffered_session_map_.find(connection_id);
  if (it == buffered_session_map_.end()) {
    return;
  }

  RemoveFromStore(*it->second);
}

void QuicBufferedPacketStore::RemoveFromStore(BufferedPacketListNode& node) {
  QUICHE_DCHECK_EQ(buffered_sessions_with_chlo_.size(),
                   num_buffered_sessions_with_chlo_);
  QUICHE_DCHECK_EQ(buffered_sessions_.size(), num_buffered_sessions_);

  // Remove |node| from all lists.
  QUIC_BUG_IF(quic_store_chlo_state_inconsistent,
              node.parsed_chlo.has_value() !=
                  buffered_sessions_with_chlo_.is_linked(&node))
      << "Inconsistent CHLO state for connection "
      << node.original_connection_id
      << ", parsed_chlo.has_value:" << node.parsed_chlo.has_value()
      << ", is_linked:" << buffered_sessions_with_chlo_.is_linked(&node);
  if (buffered_sessions_with_chlo_.is_linked(&node)) {
    buffered_sessions_with_chlo_.erase(&node);
    --num_buffered_sessions_with_chlo_;
  }

  if (buffered_sessions_.is_linked(&node)) {
    buffered_sessions_.erase(&node);
    --num_buffered_sessions_;
  } else {
    QUIC_BUG(quic_store_missing_node_in_main_list)
        << "Missing node in main buffered session list for connection "
        << node.original_connection_id;
  }

  if (node.HasReplacedConnectionId()) {
    bool erased = buffered_session_map_.erase(*node.replaced_connection_id) > 0;
    QUIC_BUG_IF(quic_store_missing_replaced_cid_in_map, !erased)
        << "Node has replaced CID but it's not in the map. original_cid: "
        << node.original_connection_id
        << " replaced_cid: " << *node.replaced_connection_id;
  }

  bool erased = buffered_session_map_.erase(node.original_connection_id) > 0;
  QUIC_BUG_IF(quic_store_missing_original_cid_in_map, !erased)
      << "Node missing in the map. original_cid: "
      << node.original_connection_id;
}

void QuicBufferedPacketStore::DiscardAllPackets() {
  buffered_sessions_with_chlo_.clear();
  num_buffered_sessions_with_chlo_ = 0;
  buffered_sessions_.clear();
  num_buffered_sessions_ = 0;
  buffered_session_map_.clear();
  expiration_alarm_->Cancel();
}

void QuicBufferedPacketStore::OnExpirationTimeout() {
  QuicTime expiration_time = clock_->ApproximateNow() - connection_life_span_;
  while (!buffered_sessions_.empty()) {
    BufferedPacketListNode& node = buffered_sessions_.front();
    if (node.creation_time > expiration_time) {
      break;
    }
    std::shared_ptr<BufferedPacketListNode> node_ref = node.shared_from_this();
    QuicConnectionId connection_id = node.original_connection_id;
    RemoveFromStore(node);
    visitor_->OnExpiredPackets(connection_id, std::move(node));
  }
  if (!buffered_sessions_.empty()) {
    MaybeSetExpirationAlarm();
  }
}

void QuicBufferedPacketStore::MaybeSetExpirationAlarm() {
  if (!expiration_alarm_->IsSet()) {
    expiration_alarm_->Set(clock_->ApproximateNow() + connection_life_span_);
  }
}

bool QuicBufferedPacketStore::ShouldNotBufferPacket(bool is_chlo) const {
  const bool is_store_full =
      num_buffered_sessions_ >= kDefaultMaxConnectionsInStore;

  if (is_chlo) {
    return is_store_full;
  }

  QUIC_BUG_IF(quic_store_too_many_connections_with_chlo,
              num_buffered_sessions_ < num_buffered_sessions_with_chlo_)
      << "num_connections: " << num_buffered_sessions_
      << ", num_connections_with_chlo: " << num_buffered_sessions_with_chlo_;
  size_t num_connections_without_chlo =
      num_buffered_sessions_ - num_buffered_sessions_with_chlo_;
  bool reach_non_chlo_limit =
      num_connections_without_chlo >= kMaxConnectionsWithoutCHLO;

  return is_store_full || reach_non_chlo_limit;
}

BufferedPacketList QuicBufferedPacketStore::DeliverPacketsForNextConnection(
    QuicConnectionId* connection_id) {
  if (buffered_sessions_with_chlo_.empty()) {
    // Returns empty list if no CHLO has been buffered.
    return BufferedPacketList();
  }

  *connection_id = buffered_sessions_with_chlo_.front().original_connection_id;
  BufferedPacketList packet_list = DeliverPackets(*connection_id);
  QUICHE_DCHECK(!packet_list.buffered_packets.empty() &&
                packet_list.parsed_chlo.has_value())
      << "Try to deliver connectons without CHLO. # packets:"
      << packet_list.buffered_packets.size()
      << ", has_parsed_chlo:" << packet_list.parsed_chlo.has_value();
  return packet_list;
}

bool QuicBufferedPacketStore::HasChloForConnection(
    QuicConnectionId connection_id) {
  auto it = buffered_session_map_.find(connection_id);
  if (it == buffered_session_map_.end()) {
    return false;
  }
  return it->second->parsed_chlo.has_value();
}

bool QuicBufferedPacketStore::IngestPacketForTlsChloExtraction(
    const QuicConnectionId& connection_id, const ParsedQuicVersion& version,
    const QuicReceivedPacket& packet,
    std::vector<uint16_t>* out_supported_groups,
    std::vector<uint16_t>* out_cert_compression_algos,
    std::vector<std::string>* out_alpns, std::string* out_sni,
    bool* out_resumption_attempted, bool* out_early_data_attempted,
    std::optional<uint8_t>* tls_alert) {
  QUICHE_DCHECK_NE(out_alpns, nullptr);
  QUICHE_DCHECK_NE(out_sni, nullptr);
  QUICHE_DCHECK_NE(tls_alert, nullptr);
  QUICHE_DCHECK_EQ(version.handshake_protocol, PROTOCOL_TLS1_3);

  auto it = buffered_session_map_.find(connection_id);
  if (it == buffered_session_map_.end()) {
    QUIC_BUG(quic_bug_10838_1)
        << "Cannot ingest packet for unknown connection ID " << connection_id;
    return false;
  }
  BufferedPacketListNode& node = *it->second;
  node.tls_chlo_extractor.IngestPacket(version, packet);
  if (!node.tls_chlo_extractor.HasParsedFullChlo()) {
    *tls_alert = node.tls_chlo_extractor.tls_alert();
    return false;
  }
  const TlsChloExtractor& tls_chlo_extractor = node.tls_chlo_extractor;
  *out_supported_groups = tls_chlo_extractor.supported_groups();
  *out_cert_compression_algos = tls_chlo_extractor.cert_compression_algos();
  *out_alpns = tls_chlo_extractor.alpns();
  *out_sni = tls_chlo_extractor.server_name();
  *out_resumption_attempted = tls_chlo_extractor.resumption_attempted();
  *out_early_data_attempted = tls_chlo_extractor.early_data_attempted();
  return true;
}

}  // namespace quic
