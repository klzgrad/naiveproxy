// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simulator/quic_endpoint_base.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "quiche/quic/core/crypto/crypto_handshake_message.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/platform/api/quic_test_output.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

// Takes a SHA-1 hash of the name and converts it into five 32-bit integers.
static std::vector<uint32_t> HashNameIntoFive32BitIntegers(std::string name) {
  const std::string hash = test::Sha1Hash(name);

  std::vector<uint32_t> output;
  uint32_t current_number = 0;
  for (size_t i = 0; i < hash.size(); i++) {
    current_number = (current_number << 8) + hash[i];
    if (i % 4 == 3) {
      output.push_back(i);
      current_number = 0;
    }
  }

  return output;
}

QuicSocketAddress GetAddressFromName(std::string name) {
  const std::vector<uint32_t> hash = HashNameIntoFive32BitIntegers(name);

  // Generate a random port between 1025 and 65535.
  const uint16_t port = 1025 + hash[0] % (65535 - 1025 + 1);

  // Generate a random 10.x.x.x address, where x is between 1 and 254.
  std::string ip_address{"\xa\0\0\0", 4};
  for (size_t i = 1; i < 4; i++) {
    ip_address[i] = 1 + hash[i] % 254;
  }
  QuicIpAddress host;
  host.FromPackedString(ip_address.c_str(), ip_address.length());
  return QuicSocketAddress(host, port);
}

QuicEndpointBase::QuicEndpointBase(Simulator* simulator, std::string name,
                                   std::string peer_name)
    : Endpoint(simulator, name),
      peer_name_(peer_name),
      writer_(this),
      nic_tx_queue_(simulator, absl::StrCat(name, " (TX Queue)"),
                    kMaxOutgoingPacketSize * kTxQueueSize),
      connection_(nullptr),
      write_blocked_count_(0),
      drop_next_packet_(false),
      connection_id_generator_(kQuicDefaultConnectionIdLength) {
  nic_tx_queue_.set_listener_interface(this);
}

QuicEndpointBase::~QuicEndpointBase() {
  if (trace_visitor_ != nullptr) {
    const char* perspective_prefix =
        connection_->perspective() == Perspective::IS_CLIENT ? "C" : "S";

    std::string identifier = absl::StrCat(
        perspective_prefix, connection_->connection_id().ToString());
    QuicRecordTrace(identifier, trace_visitor_->trace()->SerializeAsString());
  }
}

void QuicEndpointBase::DropNextIncomingPacket() { drop_next_packet_ = true; }

void QuicEndpointBase::RecordTrace() {
  trace_visitor_ = std::make_unique<QuicTraceVisitor>(connection_.get());
  connection_->set_debug_visitor(trace_visitor_.get());
}

void QuicEndpointBase::AcceptPacket(std::unique_ptr<Packet> packet) {
  if (packet->destination != name_) {
    return;
  }
  if (drop_next_packet_) {
    drop_next_packet_ = false;
    return;
  }

  QuicReceivedPacket received_packet(packet->contents.data(),
                                     packet->contents.size(), clock_->Now());
  connection_->ProcessUdpPacket(connection_->self_address(),
                                connection_->peer_address(), received_packet);
}

UnconstrainedPortInterface* QuicEndpointBase::GetRxPort() { return this; }

void QuicEndpointBase::SetTxPort(ConstrainedPortInterface* port) {
  // Any egress done by the endpoint is actually handled by a queue on an NIC.
  nic_tx_queue_.set_tx_port(port);
}

void QuicEndpointBase::OnPacketDequeued() {
  if (writer_.IsWriteBlocked() &&
      (nic_tx_queue_.capacity() - nic_tx_queue_.bytes_queued()) >=
          kMaxOutgoingPacketSize) {
    writer_.SetWritable();
    connection_->OnCanWrite();
  }
}

QuicEndpointBase::Writer::Writer(QuicEndpointBase* endpoint)
    : endpoint_(endpoint), is_blocked_(false) {}

QuicEndpointBase::Writer::~Writer() {}

WriteResult QuicEndpointBase::Writer::WritePacket(
    const char* buffer, size_t buf_len, const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/, PerPacketOptions* options,
    const QuicPacketWriterParams& /*params*/) {
  QUICHE_DCHECK(!IsWriteBlocked());
  QUICHE_DCHECK(options == nullptr);
  QUICHE_DCHECK(buf_len <= kMaxOutgoingPacketSize);

  // Instead of losing a packet, become write-blocked when the egress queue is
  // full.
  if (endpoint_->nic_tx_queue_.packets_queued() > kTxQueueSize) {
    is_blocked_ = true;
    endpoint_->write_blocked_count_++;
    return WriteResult(WRITE_STATUS_BLOCKED, 0);
  }

  auto packet = std::make_unique<Packet>();
  packet->source = endpoint_->name();
  packet->destination = endpoint_->peer_name_;
  packet->tx_timestamp = endpoint_->clock_->Now();

  packet->contents = std::string(buffer, buf_len);
  packet->size = buf_len;

  endpoint_->nic_tx_queue_.AcceptPacket(std::move(packet));

  return WriteResult(WRITE_STATUS_OK, buf_len);
}

bool QuicEndpointBase::Writer::IsWriteBlocked() const { return is_blocked_; }

void QuicEndpointBase::Writer::SetWritable() { is_blocked_ = false; }

std::optional<int> QuicEndpointBase::Writer::MessageTooBigErrorCode() const {
  return std::nullopt;
}

QuicByteCount QuicEndpointBase::Writer::GetMaxPacketSize(
    const QuicSocketAddress& /*peer_address*/) const {
  return kMaxOutgoingPacketSize;
}

bool QuicEndpointBase::Writer::SupportsReleaseTime() const { return false; }

bool QuicEndpointBase::Writer::IsBatchMode() const { return false; }

QuicPacketBuffer QuicEndpointBase::Writer::GetNextWriteLocation(
    const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/) {
  return {nullptr, nullptr};
}

WriteResult QuicEndpointBase::Writer::Flush() {
  return WriteResult(WRITE_STATUS_OK, 0);
}

QuicEndpointMultiplexer::QuicEndpointMultiplexer(
    std::string name, const std::vector<QuicEndpointBase*>& endpoints)
    : Endpoint((*endpoints.begin())->simulator(), name) {
  for (QuicEndpointBase* endpoint : endpoints) {
    mapping_.insert(std::make_pair(endpoint->name(), endpoint));
  }
}

QuicEndpointMultiplexer::~QuicEndpointMultiplexer() {}

void QuicEndpointMultiplexer::AcceptPacket(std::unique_ptr<Packet> packet) {
  auto key_value_pair_it = mapping_.find(packet->destination);
  if (key_value_pair_it == mapping_.end()) {
    return;
  }

  key_value_pair_it->second->GetRxPort()->AcceptPacket(std::move(packet));
}
UnconstrainedPortInterface* QuicEndpointMultiplexer::GetRxPort() {
  return this;
}
void QuicEndpointMultiplexer::SetTxPort(ConstrainedPortInterface* port) {
  for (auto& key_value_pair : mapping_) {
    key_value_pair.second->SetTxPort(port);
  }
}

}  // namespace simulator
}  // namespace quic
