// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_time_wait_list_manager.h"

#include <errno.h>

#include <memory>

#include "base/macros.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/crypto/quic_encrypter.h"
#include "net/quic/core/quic_framer.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_map_util.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/platform/api/quic_socket_address.h"

namespace net {

// A very simple alarm that just informs the QuicTimeWaitListManager to clean
// up old connection_ids. This alarm should be cancelled and deleted before
// the QuicTimeWaitListManager is deleted.
class ConnectionIdCleanUpAlarm : public QuicAlarm::Delegate {
 public:
  explicit ConnectionIdCleanUpAlarm(
      QuicTimeWaitListManager* time_wait_list_manager)
      : time_wait_list_manager_(time_wait_list_manager) {}

  void OnAlarm() override {
    time_wait_list_manager_->CleanUpOldConnectionIds();
  }

 private:
  // Not owned.
  QuicTimeWaitListManager* time_wait_list_manager_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionIdCleanUpAlarm);
};

// This class stores pending public reset packets to be sent to clients.
// server_address - server address on which a packet what was received for
//                  a connection_id in time wait state.
// client_address - address of the client that sent that packet. Needed to send
//                  the public reset packet back to the client.
// packet - the pending public reset packet that is to be sent to the client.
//          created instance takes the ownership of this packet.
class QuicTimeWaitListManager::QueuedPacket {
 public:
  QueuedPacket(const QuicSocketAddress& server_address,
               const QuicSocketAddress& client_address,
               std::unique_ptr<QuicEncryptedPacket> packet)
      : server_address_(server_address),
        client_address_(client_address),
        packet_(std::move(packet)) {}

  const QuicSocketAddress& server_address() const { return server_address_; }
  const QuicSocketAddress& client_address() const { return client_address_; }
  QuicEncryptedPacket* packet() { return packet_.get(); }

 private:
  const QuicSocketAddress server_address_;
  const QuicSocketAddress client_address_;
  std::unique_ptr<QuicEncryptedPacket> packet_;

  DISALLOW_COPY_AND_ASSIGN(QueuedPacket);
};

QuicTimeWaitListManager::QuicTimeWaitListManager(
    QuicPacketWriter* writer,
    Visitor* visitor,
    QuicConnectionHelperInterface* helper,
    QuicAlarmFactory* alarm_factory)
    : time_wait_period_(
          QuicTime::Delta::FromSeconds(FLAGS_quic_time_wait_list_seconds)),
      connection_id_clean_up_alarm_(
          alarm_factory->CreateAlarm(new ConnectionIdCleanUpAlarm(this))),
      clock_(helper->GetClock()),
      writer_(writer),
      visitor_(visitor) {
  SetConnectionIdCleanUpAlarm();
}

QuicTimeWaitListManager::~QuicTimeWaitListManager() {
  connection_id_clean_up_alarm_->Cancel();
}

void QuicTimeWaitListManager::AddConnectionIdToTimeWait(
    QuicConnectionId connection_id,
    QuicTransportVersion version,
    bool connection_rejected_statelessly,
    std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets) {
  if (connection_rejected_statelessly) {
    DCHECK(termination_packets != nullptr && !termination_packets->empty())
        << "Connections that were rejected statelessly must "
        << "have a close packet.  connection_id = " << connection_id;
  }
  int num_packets = 0;
  ConnectionIdMap::iterator it = connection_id_map_.find(connection_id);
  const bool new_connection_id = it == connection_id_map_.end();
  if (!new_connection_id) {  // Replace record if it is reinserted.
    num_packets = it->second.num_packets;
    connection_id_map_.erase(it);
  }
  TrimTimeWaitListIfNeeded();
  DCHECK_LT(num_connections(),
            static_cast<size_t>(FLAGS_quic_time_wait_list_max_connections));
  ConnectionIdData data(num_packets, version, clock_->ApproximateNow(),
                        connection_rejected_statelessly);
  if (termination_packets != nullptr) {
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

QuicTransportVersion QuicTimeWaitListManager::GetQuicVersionFromConnectionId(
    QuicConnectionId connection_id) {
  ConnectionIdMap::iterator it = connection_id_map_.find(connection_id);
  DCHECK(it != connection_id_map_.end());
  return (it->second).version;
}

void QuicTimeWaitListManager::OnBlockedWriterCanWrite() {
  while (!pending_packets_queue_.empty()) {
    QueuedPacket* queued_packet = pending_packets_queue_.front().get();
    if (!WriteToWire(queued_packet)) {
      return;
    }
    pending_packets_queue_.pop_front();
  }
}

void QuicTimeWaitListManager::ProcessPacket(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    QuicConnectionId connection_id) {
  DCHECK(IsConnectionIdInTimeWait(connection_id));
  QUIC_DLOG(INFO) << "Processing " << connection_id << " in time wait state.";
  // TODO(satyamshekhar): Think about handling packets from different client
  // addresses.
  ConnectionIdMap::iterator it = connection_id_map_.find(connection_id);
  DCHECK(it != connection_id_map_.end());
  // Increment the received packet count.
  ConnectionIdData* connection_data = &it->second;
  ++(connection_data->num_packets);

  if (!ShouldSendResponse(connection_data->num_packets)) {
    return;
  }

  if (!connection_data->termination_packets.empty()) {
    if (connection_data->connection_rejected_statelessly) {
      QUIC_DVLOG(3)
          << "Time wait list sending previous stateless reject response "
          << "for connection " << connection_id;
    }
    for (const auto& packet : connection_data->termination_packets) {
      SendOrQueuePacket(QuicMakeUnique<QueuedPacket>(
          server_address, client_address, packet->Clone()));
    }
    return;
  }

  SendPublicReset(server_address, client_address, connection_id);
}

void QuicTimeWaitListManager::SendVersionNegotiationPacket(
    QuicConnectionId connection_id,
    const QuicTransportVersionVector& supported_versions,
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address) {
  SendOrQueuePacket(
      QuicMakeUnique<QueuedPacket>(server_address, client_address,
                                   QuicFramer::BuildVersionNegotiationPacket(
                                       connection_id, supported_versions)));
}

// Returns true if the number of packets received for this connection_id is a
// power of 2 to throttle the number of public reset packets we send to a
// client.
bool QuicTimeWaitListManager::ShouldSendResponse(int received_packet_count) {
  return (received_packet_count & (received_packet_count - 1)) == 0;
}

void QuicTimeWaitListManager::SendPublicReset(
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    QuicConnectionId connection_id) {
  QuicPublicResetPacket packet;
  packet.public_header.connection_id = connection_id;
  packet.public_header.reset_flag = true;
  packet.public_header.version_flag = false;
  // TODO(satyamshekhar): generate a valid nonce for this connection_id.
  packet.nonce_proof = 1010101;
  packet.client_address = client_address;
  // Takes ownership of the packet.
  SendOrQueuePacket(QuicMakeUnique<QueuedPacket>(server_address, client_address,
                                                 BuildPublicReset(packet)));
}

std::unique_ptr<QuicEncryptedPacket> QuicTimeWaitListManager::BuildPublicReset(
    const QuicPublicResetPacket& packet) {
  return QuicFramer::BuildPublicResetPacket(packet);
}

// Either sends the packet and deletes it or makes pending queue the
// owner of the packet.
void QuicTimeWaitListManager::SendOrQueuePacket(
    std::unique_ptr<QueuedPacket> packet) {
  if (WriteToWire(packet.get())) {
    // Allow the packet to be deleted upon leaving this function.
    return;
  }
  pending_packets_queue_.push_back(std::move(packet));
}

bool QuicTimeWaitListManager::WriteToWire(QueuedPacket* queued_packet) {
  if (writer_->IsWriteBlocked()) {
    visitor_->OnWriteBlocked(this);
    return false;
  }
  WriteResult result = writer_->WritePacket(
      queued_packet->packet()->data(), queued_packet->packet()->length(),
      queued_packet->server_address().host(), queued_packet->client_address(),
      nullptr);
  if (result.status == WRITE_STATUS_BLOCKED) {
    // If blocked and unbuffered, return false to retry sending.
    DCHECK(writer_->IsWriteBlocked());
    visitor_->OnWriteBlocked(this);
    return writer_->IsWriteBlockedDataBuffered();
  } else if (result.status == WRITE_STATUS_ERROR) {
    QUIC_LOG_FIRST_N(WARNING, 1)
        << "Received unknown error while sending reset packet to "
        << queued_packet->client_address().ToString() << ": "
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
  ConnectionIdMap::iterator it = connection_id_map_.begin();
  QuicTime oldest_connection_id_time = it->second.time_added;
  if (oldest_connection_id_time > expiration_time) {
    // Too recent, don't retire.
    return false;
  }
  // This connection_id has lived its age, retire it now.
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
  if (FLAGS_quic_time_wait_list_max_connections < 0) {
    return;
  }
  while (num_connections() >=
         static_cast<size_t>(FLAGS_quic_time_wait_list_max_connections)) {
    MaybeExpireOldestConnection(QuicTime::Infinite());
  }
}

QuicTimeWaitListManager::ConnectionIdData::ConnectionIdData(
    int num_packets_,
    QuicTransportVersion version_,
    QuicTime time_added_,
    bool connection_rejected_statelessly)
    : num_packets(num_packets_),
      version(version_),
      time_added(time_added_),
      connection_rejected_statelessly(connection_rejected_statelessly) {}

QuicTimeWaitListManager::ConnectionIdData::ConnectionIdData(
    ConnectionIdData&& other) = default;

QuicTimeWaitListManager::ConnectionIdData::~ConnectionIdData() {}

}  // namespace net
