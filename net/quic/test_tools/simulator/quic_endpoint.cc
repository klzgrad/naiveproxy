// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/simulator/quic_endpoint.h"

#include "base/sha1.h"
#include "net/quic/core/crypto/crypto_handshake_message.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/platform/api/quic_str_cat.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "net/quic/test_tools/simulator/simulator.h"

using std::string;

namespace net {
namespace simulator {

const QuicStreamId kDataStream = 3;
const QuicByteCount kWriteChunkSize = 128 * 1024;
const char kStreamDataContents = 'Q';

// Takes a SHA-1 hash of the name and converts it into five 32-bit integers.
static std::vector<uint32_t> HashNameIntoFive32BitIntegers(string name) {
  const string hash = test::Sha1Hash(name);

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

QuicSocketAddress GetAddressFromName(string name) {
  const std::vector<uint32_t> hash = HashNameIntoFive32BitIntegers(name);

  // Generate a random port between 1025 and 65535.
  const uint16_t port = 1025 + hash[0] % (65535 - 1025 + 1);

  // Generate a random 10.x.x.x address, where x is between 1 and 254.
  string ip_address{"\xa\0\0\0", 4};
  for (size_t i = 1; i < 4; i++) {
    ip_address[i] = 1 + hash[i] % 254;
  }
  QuicIpAddress host;
  host.FromPackedString(ip_address.c_str(), ip_address.length());
  return QuicSocketAddress(host, port);
}

QuicEndpoint::QuicEndpoint(Simulator* simulator,
                           string name,
                           string peer_name,
                           Perspective perspective,
                           QuicConnectionId connection_id)
    : Endpoint(simulator, name),
      peer_name_(peer_name),
      writer_(this),
      nic_tx_queue_(simulator,
                    QuicStringPrintf("%s (TX Queue)", name.c_str()),
                    kMaxPacketSize * kTxQueueSize),
      connection_(connection_id,
                  GetAddressFromName(peer_name),
                  simulator,
                  simulator->GetAlarmFactory(),
                  &writer_,
                  false,
                  perspective,
                  CurrentSupportedTransportVersions()),
      bytes_to_transfer_(0),
      bytes_transferred_(0),
      write_blocked_count_(0),
      wrong_data_received_(false),
      transmission_buffer_(new char[kWriteChunkSize]) {
  nic_tx_queue_.set_listener_interface(this);

  connection_.SetSelfAddress(GetAddressFromName(name));
  connection_.set_visitor(this);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           new NullEncrypter(perspective));
  connection_.SetDecrypter(ENCRYPTION_FORWARD_SECURE,
                           new NullDecrypter(perspective));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);

  // Configure the connection as if it received a handshake.  This is important
  // primarily because
  //  - this enables pacing, and
  //  - this sets the non-handshake timeouts.
  std::string error;
  CryptoHandshakeMessage peer_hello;
  peer_hello.SetValue(kICSL,
                      static_cast<uint32_t>(kMaximumIdleTimeoutSecs - 1));
  QuicConfig config;
  QuicErrorCode error_code = config.ProcessPeerHello(
      peer_hello, perspective == Perspective::IS_CLIENT ? SERVER : CLIENT,
      &error);
  DCHECK_EQ(error_code, QUIC_NO_ERROR) << "Configuration failed: " << error;
  connection_.SetFromConfig(config);
}

QuicEndpoint::~QuicEndpoint() {}

void QuicEndpoint::AddBytesToTransfer(QuicByteCount bytes) {
  if (bytes_to_transfer_ > 0) {
    Schedule(clock_->Now());
  }

  bytes_to_transfer_ += bytes;
  WriteStreamData();
}

void QuicEndpoint::AcceptPacket(std::unique_ptr<Packet> packet) {
  if (packet->destination != name_) {
    return;
  }

  QuicReceivedPacket received_packet(packet->contents.data(),
                                     packet->contents.size(), clock_->Now());
  connection_.ProcessUdpPacket(connection_.self_address(),
                               connection_.peer_address(), received_packet);
}

UnconstrainedPortInterface* QuicEndpoint::GetRxPort() {
  return this;
}

void QuicEndpoint::SetTxPort(ConstrainedPortInterface* port) {
  // Any egress done by the endpoint is actually handled by a queue on an NIC.
  nic_tx_queue_.set_tx_port(port);
}

void QuicEndpoint::OnPacketDequeued() {
  if (writer_.IsWriteBlocked() &&
      (nic_tx_queue_.capacity() - nic_tx_queue_.bytes_queued()) >=
          kMaxPacketSize) {
    writer_.SetWritable();
    connection_.OnCanWrite();
  }
}

void QuicEndpoint::OnStreamFrame(const QuicStreamFrame& frame) {
  // Verify that the data received always matches the output of DataAtOffset().
  DCHECK(frame.stream_id == kDataStream);
  for (size_t i = 0; i < frame.data_length; i++) {
    if (frame.data_buffer[i] != kStreamDataContents) {
      wrong_data_received_ = true;
    }
  }
}
void QuicEndpoint::OnCanWrite() {
  WriteStreamData();
}
bool QuicEndpoint::WillingAndAbleToWrite() const {
  return bytes_to_transfer_ != 0;
}
bool QuicEndpoint::HasPendingHandshake() const {
  return false;
}
bool QuicEndpoint::HasOpenDynamicStreams() const {
  return true;
}

QuicEndpoint::Writer::Writer(QuicEndpoint* endpoint)
    : endpoint_(endpoint), is_blocked_(false) {}

QuicEndpoint::Writer::~Writer() {}

WriteResult QuicEndpoint::Writer::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  DCHECK(!IsWriteBlocked());
  DCHECK(options == nullptr);
  DCHECK(buf_len <= kMaxPacketSize);

  // Instead of losing a packet, become write-blocked when the egress queue is
  // full.
  if (endpoint_->nic_tx_queue_.packets_queued() > kTxQueueSize) {
    is_blocked_ = true;
    endpoint_->write_blocked_count_++;
    return WriteResult(WRITE_STATUS_BLOCKED, 0);
  }

  auto packet = QuicMakeUnique<Packet>();
  packet->source = endpoint_->name();
  packet->destination = endpoint_->peer_name_;
  packet->tx_timestamp = endpoint_->clock_->Now();

  packet->contents = string(buffer, buf_len);
  packet->size = buf_len;

  endpoint_->nic_tx_queue_.AcceptPacket(std::move(packet));

  return WriteResult(WRITE_STATUS_OK, buf_len);
}

bool QuicEndpoint::Writer::IsWriteBlockedDataBuffered() const {
  return false;
}
bool QuicEndpoint::Writer::IsWriteBlocked() const {
  return is_blocked_;
}
void QuicEndpoint::Writer::SetWritable() {
  is_blocked_ = false;
}
QuicByteCount QuicEndpoint::Writer::GetMaxPacketSize(
    const QuicSocketAddress& /*peer_address*/) const {
  return kMaxPacketSize;
}

void QuicEndpoint::WriteStreamData() {
  // Instantiate a bundler which would normally be here due to QuicSession.
  QuicConnection::ScopedPacketBundler packet_bundler(
      &connection_, QuicConnection::SEND_ACK_IF_QUEUED);

  while (bytes_to_transfer_ > 0) {
    // Transfer data in chunks of size at most |kWriteChunkSize|.
    const size_t transmission_size =
        std::min(kWriteChunkSize, bytes_to_transfer_);
    memset(transmission_buffer_.get(), kStreamDataContents, transmission_size);

    iovec iov;
    iov.iov_base = transmission_buffer_.get();
    iov.iov_len = transmission_size;

    QuicIOVector io_vector(&iov, 1, transmission_size);
    QuicConsumedData consumed_data = connection_.SendStreamData(
        kDataStream, io_vector, bytes_transferred_, NO_FIN, nullptr);

    DCHECK(consumed_data.bytes_consumed <= transmission_size);
    bytes_transferred_ += consumed_data.bytes_consumed;
    bytes_to_transfer_ -= consumed_data.bytes_consumed;
    if (consumed_data.bytes_consumed != transmission_size) {
      return;
    }
  }
}

QuicEndpointMultiplexer::QuicEndpointMultiplexer(
    string name,
    std::initializer_list<QuicEndpoint*> endpoints)
    : Endpoint((*endpoints.begin())->simulator(), name) {
  for (QuicEndpoint* endpoint : endpoints) {
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
}  // namespace net
