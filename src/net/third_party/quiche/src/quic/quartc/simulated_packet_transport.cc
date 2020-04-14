// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/simulated_packet_transport.h"

#include <utility>

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {
namespace simulator {

SimulatedQuartcPacketTransport::SimulatedQuartcPacketTransport(
    Simulator* simulator,
    const std::string& name,
    const std::string& peer_name,
    QuicByteCount queue_capacity)
    : Endpoint(simulator, name),
      peer_name_(peer_name),
      egress_queue_(simulator,
                    quiche::QuicheStringPrintf("%s (TX Queue)", name.c_str()),
                    queue_capacity) {
  egress_queue_.set_listener_interface(this);
}

int SimulatedQuartcPacketTransport::Write(const char* buffer,
                                          size_t buf_len,
                                          const PacketInfo& info) {
  if (!writable_) {
    return 0;
  }
  if (egress_queue_.bytes_queued() + buf_len > egress_queue_.capacity()) {
    return 0;
  }

  last_packet_number_ = info.packet_number;

  auto packet = std::make_unique<Packet>();
  packet->contents = std::string(buffer, buf_len);
  packet->size = buf_len;
  packet->tx_timestamp = clock_->Now();
  packet->source = name();
  packet->destination = peer_name_;

  egress_queue_.AcceptPacket(std::move(packet));
  return buf_len;
}

void SimulatedQuartcPacketTransport::SetDelegate(Delegate* delegate) {
  delegate_ = delegate;
  Schedule(clock_->Now());
}

UnconstrainedPortInterface* SimulatedQuartcPacketTransport::GetRxPort() {
  return this;
}

void SimulatedQuartcPacketTransport::SetTxPort(ConstrainedPortInterface* port) {
  egress_queue_.set_tx_port(port);
  Schedule(clock_->Now());
}

void SimulatedQuartcPacketTransport::AcceptPacket(
    std::unique_ptr<Packet> packet) {
  // Simulated switches broadcast packets to all ports if the cannot determine
  // the recipient, so we need to drop packets that aren't intended for us.
  if (packet->destination != name()) {
    return;
  }

  if (delegate_) {
    delegate_->OnTransportReceived(packet->contents.data(), packet->size);
  }
}

void SimulatedQuartcPacketTransport::OnPacketDequeued() {
  if (delegate_ && writable_) {
    delegate_->OnTransportCanWrite();
  }
}

void SimulatedQuartcPacketTransport::Act() {
  if (delegate_ && writable_) {
    delegate_->OnTransportCanWrite();
  }
}

void SimulatedQuartcPacketTransport::SetWritable(bool writable) {
  writable_ = writable;
  if (writable_) {
    // May need to call |Delegate::OnTransportCanWrite|.
    Schedule(clock_->Now());
  }
}

}  // namespace simulator
}  // namespace quic
