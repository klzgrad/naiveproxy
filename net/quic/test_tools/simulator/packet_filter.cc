// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/simulator/packet_filter.h"

using std::string;

namespace net {
namespace simulator {

PacketFilter::PacketFilter(Simulator* simulator, string name, Endpoint* input)
    : Endpoint(simulator, name), input_(input) {
  input_->SetTxPort(this);
}

PacketFilter::~PacketFilter() {}

void PacketFilter::AcceptPacket(std::unique_ptr<Packet> packet) {
  if (FilterPacket(*packet)) {
    output_tx_port_->AcceptPacket(std::move(packet));
  }
}

QuicTime::Delta PacketFilter::TimeUntilAvailable() {
  return output_tx_port_->TimeUntilAvailable();
}

void PacketFilter::Act() {}

UnconstrainedPortInterface* PacketFilter::GetRxPort() {
  return input_->GetRxPort();
}

void PacketFilter::SetTxPort(ConstrainedPortInterface* port) {
  output_tx_port_ = port;
}

}  // namespace simulator
}  // namespace net
