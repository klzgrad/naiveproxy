// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_time_wait_list_manager.h"

#include <errno.h>

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

// A very simple alarm that just informs the QuicTimeWaitListManager to clean
// up old connection_ids. This alarm should be cancelled and deleted before
// the QuicTimeWaitListManager is deleted.
class ConnectionIdCleanUpAlarm : public QuicAlarm::DelegateWithoutContext {
 public:
  explicit ConnectionIdCleanUpAlarm(
      QuicTimeWaitListManager* time_wait_list_manager)
      : time_wait_list_manager_(time_wait_list_manager) {}
  ConnectionIdCleanUpAlarm(const ConnectionIdCleanUpAlarm&) = delete;
  ConnectionIdCleanUpAlarm& operator=(const ConnectionIdCleanUpAlarm&) = delete;

  void OnAlarm() override {
    time_wait_list_manager_->CleanUpOldConnectionIds();
  }

 private:
  // Not owned.
  QuicTimeWaitListManager* time_wait_list_manager_;
};

TimeWaitConnectionInfo::TimeWaitConnectionInfo(
    bool ietf_quic,
    std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets,
    std::vector<QuicConnectionId> active_connection_ids)
    : TimeWaitConnectionInfo(ietf_quic, termination_packets,
                             std::move(active_connection_ids),
                             QuicTime::Delta::Zero()) {}

TimeWaitConnectionInfo::TimeWaitConnectionInfo(
    bool ietf_quic,
    std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets,
    std::vector<QuicConnectionId> active_connection_ids, QuicTime::Delta srtt)
    : ietf_quic(ietf_quic),
      active_connection_ids(std::move(active_connection_ids)),
      srtt(srtt) {
  if (termination_packets != nullptr) {
    this->termination_packets.swap(*termination_packets);
  }
}

QuicTimeWaitListManager::QuicTimeWaitListManager(
    QuicPacketWriter* writer, Visitor* visitor, const QuicClock* clock,
    QuicAlarmFactory* alarm_factory)
    : time_wait_period_(QuicTime::Delta::FromSeconds(
          GetQuicFlag(quic_time_wait_list_seconds))),
      connection_id_clean_up_alarm_(
          alarm_factory->CreateAlarm(new ConnectionIdCleanUpAlarm(this))),
      clock_(clock),
      writer_(writer),
      visitor_(visitor) {
  SetConnectionIdCleanUpAlarm();
}

QuicTimeWaitListManager::~QuicTimeWaitListManager() {
  connection_id_clean_up_alarm_->Cancel();
}

QuicTimeWaitListManager::ConnectionIdMap::iterator
QuicTimeWaitListManager::FindConnectionIdDataInMap(
    const QuicConnectionId& connection_id) {
  auto it = indirect_connection_id_map_.find(connection_id);
  if (it == indirect_connection_id_map_.end()) {
    return connection_id_map_.end();
  }
  return connection_id_map_.find(it->second);
}

void QuicTimeWaitListManager::AddConnectionIdDataToMap(
    const QuicConnectionId& canonical_connection_id, int num_packets,
    TimeWaitAction action, TimeWaitConnectionInfo info) {
  for (const auto& cid : info.active_connection_ids) {
    indirect_connection_id_map_[cid] = canonical_connection_id;
  }
  ConnectionIdData data(num_packets, clock_->ApproximateNow(), action,
                        std::move(info));
  connection_id_map_.emplace(
      std::make_pair(canonical_connection_id, std::move(data)));
}

void QuicTimeWaitListManager::RemoveConnectionDataFromMap(
    ConnectionIdMap::iterator it) {
  for (const auto& cid : it->second.info.active_connection_ids) {
    indirect_connection_id_map_.erase(cid);
  }
  connection_id_map_.erase(it);
}

void QuicTimeWaitListManager::AddConnectionIdToTimeWait(
    TimeWaitAction action, TimeWaitConnectionInfo info) {
  QUICHE_DCHECK(!info.active_connection_ids.empty());
  const QuicConnectionId& canonical_connection_id =
      info.active_connection_ids.front();
  QUICHE_DCHECK(action != SEND_TERMINATION_PACKETS ||
                !info.termination_packets.empty());
  QUICHE_DCHECK(action != DO_NOTHING || info.ietf_quic);
  int num_packets = 0;
  auto it = FindConnectionIdDataInMap(canonical_connection_id);
  const bool new_connection_id = it == connection_id_map_.end();
  if (!new_connection_id) {  // Replace record if it is reinserted.
    num_packets = it->second.num_packets;
    RemoveConnectionDataFromMap(it);
  }
  TrimTimeWaitListIfNeeded();
  int64_t max_connections = GetQuicFlag(quic_time_wait_list_max_connections);
  QUICHE_DCHECK(connection_id_map_.empty() ||
                num_connections() < static_cast<size_t>(max_connections));
  if (new_connection_id) {
    for (const auto& cid : info.active_connection_ids) {
      visitor_->OnConnectionAddedToTimeWaitList(cid);
    }
  }
  AddConnectionIdDataToMap(canonical_connection_id, num_packets, action,
                           std::move(info));
}

bool QuicTimeWaitListManager::IsConnectionIdInTimeWait(
    QuicConnectionId connection_id) const {
  return indirect_connection_id_map_.contains(connection_id);
}

void QuicTimeWaitListManager::OnBlockedWriterCanWrite() {
  writer_->SetWritable();
  while (!pending_packets_queue_.empty()) {
    QueuedPacket* queued_packet = pending_packets_queue_.front().get();
    if (!WriteToWire(queued_packet)) {
      return;
    }
    pending_packets_queue_.pop_front();
  }
}

void QuicTimeWaitListManager::ProcessPacket(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, QuicConnectionId connection_id,
    PacketHeaderFormat header_format, size_t received_packet_length,
    std::unique_ptr<QuicPerPacketContext> packet_context) {
  QUICHE_DCHECK(IsConnectionIdInTimeWait(connection_id));
  // TODO(satyamshekhar): Think about handling packets from different peer
  // addresses.
  auto it = FindConnectionIdDataInMap(connection_id);
  QUICHE_DCHECK(it != connection_id_map_.end());
  // Increment the received packet count.
  ConnectionIdData* connection_data = &it->second;
  ++(connection_data->num_packets);
  const QuicTime now = clock_->ApproximateNow();
  QuicTime::Delta delta = QuicTime::Delta::Zero();
  if (now > connection_data->time_added) {
    delta = now - connection_data->time_added;
  }
  OnPacketReceivedForKnownConnection(connection_data->num_packets, delta,
                                     connection_data->info.srtt);

  if (!ShouldSendResponse(connection_data->num_packets)) {
    QUIC_DLOG(INFO) << "Processing " << connection_id << " in time wait state: "
                    << "throttled";
    return;
  }

  QUIC_DLOG(INFO) << "Processing " << connection_id << " in time wait state: "
                  << "header format=" << header_format
                  << " ietf=" << connection_data->info.ietf_quic
                  << ", action=" << connection_data->action
                  << ", number termination packets="
                  << connection_data->info.termination_packets.size();
  switch (connection_data->action) {
    case SEND_TERMINATION_PACKETS:
      if (connection_data->info.termination_packets.empty()) {
        QUIC_BUG(quic_bug_10608_1) << "There are no termination packets.";
        return;
      }
      switch (header_format) {
        case IETF_QUIC_LONG_HEADER_PACKET:
          if (!connection_data->info.ietf_quic) {
            QUIC_CODE_COUNT(quic_received_long_header_packet_for_gquic);
          }
          break;
        case IETF_QUIC_SHORT_HEADER_PACKET:
          if (!connection_data->info.ietf_quic) {
            QUIC_CODE_COUNT(quic_received_short_header_packet_for_gquic);
          }
          // Send stateless reset in response to short header packets.
          SendPublicReset(self_address, peer_address, connection_id,
                          connection_data->info.ietf_quic,
                          received_packet_length, std::move(packet_context));
          return;
        case GOOGLE_QUIC_PACKET:
          if (connection_data->info.ietf_quic) {
            QUIC_CODE_COUNT(quic_received_gquic_packet_for_ietf_quic);
          }
          break;
      }

      for (const auto& packet : connection_data->info.termination_packets) {
        SendOrQueuePacket(std::make_unique<QueuedPacket>(
                              self_address, peer_address, packet->Clone()),
                          packet_context.get());
      }
      return;

    case SEND_CONNECTION_CLOSE_PACKETS:
      if (connection_data->info.termination_packets.empty()) {
        QUIC_BUG(quic_bug_10608_2) << "There are no termination packets.";
        return;
      }
      for (const auto& packet : connection_data->info.termination_packets) {
        SendOrQueuePacket(std::make_unique<QueuedPacket>(
                              self_address, peer_address, packet->Clone()),
                          packet_context.get());
      }
      return;

    case SEND_STATELESS_RESET:
      if (header_format == IETF_QUIC_LONG_HEADER_PACKET) {
        QUIC_CODE_COUNT(quic_stateless_reset_long_header_packet);
      }
      SendPublicReset(self_address, peer_address, connection_id,
                      connection_data->info.ietf_quic, received_packet_length,
                      std::move(packet_context));
      return;
    case DO_NOTHING:
      QUIC_CODE_COUNT(quic_time_wait_list_do_nothing);
      QUICHE_DCHECK(connection_data->info.ietf_quic);
  }
}

void QuicTimeWaitListManager::SendVersionNegotiationPacket(
    QuicConnectionId server_connection_id,
    QuicConnectionId client_connection_id, bool ietf_quic,
    bool use_length_prefix, const ParsedQuicVersionVector& supported_versions,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    std::unique_ptr<QuicPerPacketContext> packet_context) {
  std::unique_ptr<QuicEncryptedPacket> version_packet =
      QuicFramer::BuildVersionNegotiationPacket(
          server_connection_id, client_connection_id, ietf_quic,
          use_length_prefix, supported_versions);
  QUIC_DVLOG(2) << "Dispatcher sending version negotiation packet {"
                << ParsedQuicVersionVectorToString(supported_versions) << "}, "
                << (ietf_quic ? "" : "!") << "ietf_quic, "
                << (use_length_prefix ? "" : "!")
                << "use_length_prefix:" << std::endl
                << quiche::QuicheTextUtils::HexDump(absl::string_view(
                       version_packet->data(), version_packet->length()));
  SendOrQueuePacket(std::make_unique<QueuedPacket>(self_address, peer_address,
                                                   std::move(version_packet)),
                    packet_context.get());
}

// Returns true if the number of packets received for this connection_id is a
// power of 2 to throttle the number of public reset packets we send to a peer.
bool QuicTimeWaitListManager::ShouldSendResponse(int received_packet_count) {
  return (received_packet_count & (received_packet_count - 1)) == 0;
}

void QuicTimeWaitListManager::SendPublicReset(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, QuicConnectionId connection_id,
    bool ietf_quic, size_t received_packet_length,
    std::unique_ptr<QuicPerPacketContext> packet_context) {
  if (ietf_quic) {
    std::unique_ptr<QuicEncryptedPacket> ietf_reset_packet =
        BuildIetfStatelessResetPacket(connection_id, received_packet_length);
    if (ietf_reset_packet == nullptr) {
      // This could happen when trying to reject a short header packet of
      // a connection which is in the time wait list (and with no termination
      // packet).
      return;
    }
    QUIC_DVLOG(2) << "Dispatcher sending IETF reset packet for "
                  << connection_id << std::endl
                  << quiche::QuicheTextUtils::HexDump(
                         absl::string_view(ietf_reset_packet->data(),
                                           ietf_reset_packet->length()));
    SendOrQueuePacket(
        std::make_unique<QueuedPacket>(self_address, peer_address,
                                       std::move(ietf_reset_packet)),
        packet_context.get());
    return;
  }
  // Google QUIC public resets donot elicit resets in response.
  QuicPublicResetPacket packet;
  packet.connection_id = connection_id;
  // TODO(satyamshekhar): generate a valid nonce for this connection_id.
  packet.nonce_proof = 1010101;
  // TODO(wub): This is wrong for proxied sessions. Fix it.
  packet.client_address = peer_address;
  GetEndpointId(&packet.endpoint_id);
  // Takes ownership of the packet.
  std::unique_ptr<QuicEncryptedPacket> reset_packet = BuildPublicReset(packet);
  QUIC_DVLOG(2) << "Dispatcher sending reset packet for " << connection_id
                << std::endl
                << quiche::QuicheTextUtils::HexDump(absl::string_view(
                       reset_packet->data(), reset_packet->length()));
  SendOrQueuePacket(std::make_unique<QueuedPacket>(self_address, peer_address,
                                                   std::move(reset_packet)),
                    packet_context.get());
}

void QuicTimeWaitListManager::SendPacket(const QuicSocketAddress& self_address,
                                         const QuicSocketAddress& peer_address,
                                         const QuicEncryptedPacket& packet) {
  SendOrQueuePacket(std::make_unique<QueuedPacket>(self_address, peer_address,
                                                   packet.Clone()),
                    nullptr);
}

std::unique_ptr<QuicEncryptedPacket> QuicTimeWaitListManager::BuildPublicReset(
    const QuicPublicResetPacket& packet) {
  return QuicFramer::BuildPublicResetPacket(packet);
}

std::unique_ptr<QuicEncryptedPacket>
QuicTimeWaitListManager::BuildIetfStatelessResetPacket(
    QuicConnectionId connection_id, size_t received_packet_length) {
  return QuicFramer::BuildIetfStatelessResetPacket(
      connection_id, received_packet_length,
      GetStatelessResetToken(connection_id));
}

// Either sends the packet and deletes it or makes pending queue the
// owner of the packet.
bool QuicTimeWaitListManager::SendOrQueuePacket(
    std::unique_ptr<QueuedPacket> packet,
    const QuicPerPacketContext* /*packet_context*/) {
  if (packet == nullptr) {
    QUIC_LOG(ERROR) << "Tried to send or queue a null packet";
    return true;
  }
  if (pending_packets_queue_.size() >=
      GetQuicFlag(quic_time_wait_list_max_pending_packets)) {
    // There are too many pending packets.
    QUIC_CODE_COUNT(quic_too_many_pending_packets_in_time_wait);
    return true;
  }
  if (WriteToWire(packet.get())) {
    // Allow the packet to be deleted upon leaving this function.
    return true;
  }
  pending_packets_queue_.push_back(std::move(packet));
  return false;
}

bool QuicTimeWaitListManager::WriteToWire(QueuedPacket* queued_packet) {
  if (writer_->IsWriteBlocked()) {
    visitor_->OnWriteBlocked(this);
    return false;
  }
  WriteResult result = writer_->WritePacket(
      queued_packet->packet()->data(), queued_packet->packet()->length(),
      queued_packet->self_address().host(), queued_packet->peer_address(),
      nullptr);

  // If using a batch writer and the packet is buffered, flush it.
  if (writer_->IsBatchMode() && result.status == WRITE_STATUS_OK &&
      result.bytes_written == 0) {
    result = writer_->Flush();
  }

  if (IsWriteBlockedStatus(result.status)) {
    // If blocked and unbuffered, return false to retry sending.
    QUICHE_DCHECK(writer_->IsWriteBlocked());
    visitor_->OnWriteBlocked(this);
    return result.status == WRITE_STATUS_BLOCKED_DATA_BUFFERED;
  } else if (IsWriteError(result.status)) {
    QUIC_LOG_FIRST_N(WARNING, 1)
        << "Received unknown error while sending termination packet to "
        << queued_packet->peer_address().ToString() << ": "
        << strerror(result.error_code);
  }
  return true;
}

void QuicTimeWaitListManager::SetConnectionIdCleanUpAlarm() {
  QuicTime::Delta next_alarm_interval = QuicTime::Delta::Zero();
  if (!connection_id_map_.empty()) {
    QuicTime oldest_connection_id =
        connection_id_map_.begin()->second.time_added;
    QuicTime now = clock_->ApproximateNow();
    if (now - oldest_connection_id < time_wait_period_) {
      next_alarm_interval = oldest_connection_id + time_wait_period_ - now;
    } else {
      QUIC_LOG(ERROR)
          << "ConnectionId lingered for longer than time_wait_period_";
    }
  } else {
    // No connection_ids added so none will expire before time_wait_period_.
    next_alarm_interval = time_wait_period_;
  }

  connection_id_clean_up_alarm_->Update(
      clock_->ApproximateNow() + next_alarm_interval, QuicTime::Delta::Zero());
}

bool QuicTimeWaitListManager::MaybeExpireOldestConnection(
    QuicTime expiration_time) {
  if (connection_id_map_.empty()) {
    return false;
  }
  auto it = connection_id_map_.begin();
  QuicTime oldest_connection_id_time = it->second.time_added;
  if (oldest_connection_id_time > expiration_time) {
    // Too recent, don't retire.
    return false;
  }
  // This connection_id has lived its age, retire it now.
  QUIC_DLOG(INFO) << "Connection " << it->first
                  << " expired from time wait list";
  RemoveConnectionDataFromMap(it);
  if (expiration_time == QuicTime::Infinite()) {
    QUIC_CODE_COUNT(quic_time_wait_list_trim_full);
  } else {
    QUIC_CODE_COUNT(quic_time_wait_list_expire_connections);
  }
  return true;
}

void QuicTimeWaitListManager::CleanUpOldConnectionIds() {
  QuicTime now = clock_->ApproximateNow();
  QuicTime expiration = now - time_wait_period_;

  while (MaybeExpireOldestConnection(expiration)) {
  }

  SetConnectionIdCleanUpAlarm();
}

void QuicTimeWaitListManager::TrimTimeWaitListIfNeeded() {
  const int64_t kMaxConnections =
      GetQuicFlag(quic_time_wait_list_max_connections);
  if (kMaxConnections < 0) {
    return;
  }
  while (!connection_id_map_.empty() &&
         num_connections() >= static_cast<size_t>(kMaxConnections)) {
    MaybeExpireOldestConnection(QuicTime::Infinite());
  }
}

QuicTimeWaitListManager::ConnectionIdData::ConnectionIdData(
    int num_packets, QuicTime time_added, TimeWaitAction action,
    TimeWaitConnectionInfo info)
    : num_packets(num_packets),
      time_added(time_added),
      action(action),
      info(std::move(info)) {}

QuicTimeWaitListManager::ConnectionIdData::ConnectionIdData(
    ConnectionIdData&& other) = default;

QuicTimeWaitListManager::ConnectionIdData::~ConnectionIdData() = default;

StatelessResetToken QuicTimeWaitListManager::GetStatelessResetToken(
    QuicConnectionId connection_id) const {
  return QuicUtils::GenerateStatelessResetToken(connection_id);
}

}  // namespace quic
