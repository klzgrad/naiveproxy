// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_time_wait_list_manager.h"

#include <errno.h>

#include <memory>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_clock.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

// A very simple alarm that just informs the QuicTimeWaitListManager to clean
// up old connection_ids. This alarm should be cancelled and deleted before
// the QuicTimeWaitListManager is deleted.
class ConnectionIdCleanUpAlarm : public QuicAlarm::Delegate {
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

QuicTimeWaitListManager::QuicTimeWaitListManager(
    QuicPacketWriter* writer,
    Visitor* visitor,
    const QuicClock* clock,
    QuicAlarmFactory* alarm_factory)
    : time_wait_period_(QuicTime::Delta::FromSeconds(
          GetQuicFlag(FLAGS_quic_time_wait_list_seconds))),
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

void QuicTimeWaitListManager::AddConnectionIdToTimeWait(
    QuicConnectionId connection_id,
    bool ietf_quic,
    TimeWaitAction action,
    EncryptionLevel encryption_level,
    std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets) {
  DCHECK(action != SEND_TERMINATION_PACKETS || termination_packets != nullptr);
  DCHECK(action != DO_NOTHING || ietf_quic);
  int num_packets = 0;
  auto it = connection_id_map_.find(connection_id);
  const bool new_connection_id = it == connection_id_map_.end();
  if (!new_connection_id) {  // Replace record if it is reinserted.
    num_packets = it->second.num_packets;
    connection_id_map_.erase(it);
  }
  TrimTimeWaitListIfNeeded();
  int64_t max_connections =
      GetQuicFlag(FLAGS_quic_time_wait_list_max_connections);
  DCHECK(connection_id_map_.empty() ||
         num_connections() < static_cast<size_t>(max_connections));
  ConnectionIdData data(num_packets, ietf_quic, clock_->ApproximateNow(),
                        action);
  if (termination_packets != nullptr) {
    data.encryption_level = encryption_level;
    data.termination_packets.swap(*termination_packets);
  }
  connection_id_map_.emplace(std::make_pair(connection_id, std::move(data)));
  if (new_connection_id) {
    visitor_->OnConnectionAddedToTimeWaitList(connection_id);
  }
}

bool QuicTimeWaitListManager::IsConnectionIdInTimeWait(
    QuicConnectionId connection_id) const {
  return QuicContainsKey(connection_id_map_, connection_id);
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
    const QuicSocketAddress& peer_address,
    QuicConnectionId connection_id,
    PacketHeaderFormat header_format,
    std::unique_ptr<QuicPerPacketContext> packet_context) {
  DCHECK(IsConnectionIdInTimeWait(connection_id));
  // TODO(satyamshekhar): Think about handling packets from different peer
  // addresses.
  auto it = connection_id_map_.find(connection_id);
  DCHECK(it != connection_id_map_.end());
  // Increment the received packet count.
  ConnectionIdData* connection_data = &it->second;
  ++(connection_data->num_packets);

  if (!ShouldSendResponse(connection_data->num_packets)) {
    QUIC_DLOG(INFO) << "Processing " << connection_id << " in time wait state: "
                    << "throttled";
    return;
  }

  QUIC_DLOG(INFO) << "Processing " << connection_id << " in time wait state: "
                  << "header format=" << header_format
                  << " ietf=" << connection_data->ietf_quic
                  << ", action=" << connection_data->action
                  << ", number termination packets="
                  << connection_data->termination_packets.size()
                  << ", encryption level=" << connection_data->encryption_level;
  switch (connection_data->action) {
    case SEND_TERMINATION_PACKETS:
      if (connection_data->termination_packets.empty()) {
        QUIC_BUG << "There are no termination packets.";
        return;
      }
      switch (header_format) {
        case IETF_QUIC_LONG_HEADER_PACKET:
          if (!connection_data->ietf_quic) {
            QUIC_CODE_COUNT(quic_received_long_header_packet_for_gquic);
          }
          if (connection_data->encryption_level == ENCRYPTION_FORWARD_SECURE) {
            QUIC_CODE_COUNT(
                quic_forward_secure_termination_packets_for_long_header);
          }
          break;
        case IETF_QUIC_SHORT_HEADER_PACKET:
          if (!connection_data->ietf_quic) {
            QUIC_CODE_COUNT(quic_received_short_header_packet_for_gquic);
          }
          if (connection_data->encryption_level == ENCRYPTION_INITIAL) {
            QUIC_CODE_COUNT(
                quic_encryption_none_termination_packets_for_short_header);
            // Send stateless reset in response to short header packets,
            // because ENCRYPTION_INITIAL termination packets will not be
            // processed by clients.
            SendPublicReset(self_address, peer_address, connection_id,
                            connection_data->ietf_quic,
                            std::move(packet_context));
            return;
          }
          if (connection_data->encryption_level == ENCRYPTION_ZERO_RTT) {
            QUIC_CODE_COUNT(quic_zero_rtt_termination_packets_for_short_header);
          }
          break;
        case GOOGLE_QUIC_PACKET:
          if (connection_data->ietf_quic) {
            QUIC_CODE_COUNT(quic_received_gquic_packet_for_ietf_quic);
          }
          break;
      }

      for (const auto& packet : connection_data->termination_packets) {
        SendOrQueuePacket(QuicMakeUnique<QueuedPacket>(
                              self_address, peer_address, packet->Clone()),
                          packet_context.get());
      }
      return;
    case SEND_STATELESS_RESET:
      if (header_format == IETF_QUIC_LONG_HEADER_PACKET) {
        QUIC_CODE_COUNT(quic_stateless_reset_long_header_packet);
      }
      SendPublicReset(self_address, peer_address, connection_id,
                      connection_data->ietf_quic, std::move(packet_context));
      return;
    case DO_NOTHING:
      QUIC_CODE_COUNT(quic_time_wait_list_do_nothing);
      DCHECK(connection_data->ietf_quic);
  }
}

void QuicTimeWaitListManager::SendVersionNegotiationPacket(
    QuicConnectionId server_connection_id,
    QuicConnectionId client_connection_id,
    bool ietf_quic,
    bool use_length_prefix,
    const ParsedQuicVersionVector& supported_versions,
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
                << QuicTextUtils::HexDump(QuicStringPiece(
                       version_packet->data(), version_packet->length()));
  SendOrQueuePacket(QuicMakeUnique<QueuedPacket>(self_address, peer_address,
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
    const QuicSocketAddress& peer_address,
    QuicConnectionId connection_id,
    bool ietf_quic,
    std::unique_ptr<QuicPerPacketContext> packet_context) {
  if (ietf_quic) {
    std::unique_ptr<QuicEncryptedPacket> ietf_reset_packet =
        BuildIetfStatelessResetPacket(connection_id);
    QUIC_DVLOG(2) << "Dispatcher sending IETF reset packet for "
                  << connection_id << std::endl
                  << QuicTextUtils::HexDump(
                         QuicStringPiece(ietf_reset_packet->data(),
                                         ietf_reset_packet->length()));
    SendOrQueuePacket(
        QuicMakeUnique<QueuedPacket>(self_address, peer_address,
                                     std::move(ietf_reset_packet)),
        packet_context.get());
    return;
  }
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
                << QuicTextUtils::HexDump(QuicStringPiece(
                       reset_packet->data(), reset_packet->length()));
  SendOrQueuePacket(QuicMakeUnique<QueuedPacket>(self_address, peer_address,
                                                 std::move(reset_packet)),
                    packet_context.get());
}

std::unique_ptr<QuicEncryptedPacket> QuicTimeWaitListManager::BuildPublicReset(
    const QuicPublicResetPacket& packet) {
  return QuicFramer::BuildPublicResetPacket(packet);
}

std::unique_ptr<QuicEncryptedPacket>
QuicTimeWaitListManager::BuildIetfStatelessResetPacket(
    QuicConnectionId connection_id) {
  return QuicFramer::BuildIetfStatelessResetPacket(
      connection_id, GetStatelessResetToken(connection_id));
}

// Either sends the packet and deletes it or makes pending queue the
// owner of the packet.
bool QuicTimeWaitListManager::SendOrQueuePacket(
    std::unique_ptr<QueuedPacket> packet,
    const QuicPerPacketContext* /*packet_context*/) {
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
    DCHECK(writer_->IsWriteBlocked());
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
  connection_id_map_.erase(it);
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
      GetQuicFlag(FLAGS_quic_time_wait_list_max_connections);
  if (kMaxConnections < 0) {
    return;
  }
  while (!connection_id_map_.empty() &&
         num_connections() >= static_cast<size_t>(kMaxConnections)) {
    MaybeExpireOldestConnection(QuicTime::Infinite());
  }
}

QuicTimeWaitListManager::ConnectionIdData::ConnectionIdData(
    int num_packets,
    bool ietf_quic,
    QuicTime time_added,
    TimeWaitAction action)
    : num_packets(num_packets),
      ietf_quic(ietf_quic),
      time_added(time_added),
      encryption_level(ENCRYPTION_INITIAL),
      action(action) {}

QuicTimeWaitListManager::ConnectionIdData::ConnectionIdData(
    ConnectionIdData&& other) = default;

QuicTimeWaitListManager::ConnectionIdData::~ConnectionIdData() = default;

QuicUint128 QuicTimeWaitListManager::GetStatelessResetToken(
    QuicConnectionId connection_id) const {
  return QuicUtils::GenerateStatelessResetToken(connection_id);
}

}  // namespace quic
