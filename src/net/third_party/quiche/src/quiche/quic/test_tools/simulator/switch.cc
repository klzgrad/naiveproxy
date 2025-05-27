// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simulator/switch.h"

#include <cinttypes>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"

namespace quic {
namespace simulator {

Switch::Switch(Simulator* simulator, std::string name,
               SwitchPortNumber port_count, QuicByteCount queue_capacity) {
  for (size_t port_number = 1; port_number <= port_count; port_number++) {
    ports_.emplace_back(simulator,
                        absl::StrCat(name, " (port ", port_number, ")"), this,
                        port_number, queue_capacity);
  }
}

Switch::~Switch() {}

Switch::Port::Port(Simulator* simulator, std::string name, Switch* parent,
                   SwitchPortNumber port_number, QuicByteCount queue_capacity)
    : Endpoint(simulator, name),
      parent_(parent),
      port_number_(port_number),
      connected_(false),
      queue_(simulator, absl::StrCat(name, " (queue)"), queue_capacity) {}

void Switch::Port::AcceptPacket(std::unique_ptr<Packet> packet) {
  parent_->DispatchPacket(port_number_, std::move(packet));
}

void Switch::Port::EnqueuePacket(std::unique_ptr<Packet> packet) {
  queue_.AcceptPacket(std::move(packet));
}

UnconstrainedPortInterface* Switch::Port::GetRxPort() { return this; }

void Switch::Port::SetTxPort(ConstrainedPortInterface* port) {
  queue_.set_tx_port(port);
  connected_ = true;
}

void Switch::Port::Act() {}

void Switch::DispatchPacket(SwitchPortNumber port_number,
                            std::unique_ptr<Packet> packet) {
  Port* source_port = &ports_[port_number - 1];
  const auto source_mapping_it = switching_table_.find(packet->source);
  if (source_mapping_it == switching_table_.end()) {
    switching_table_.insert(std::make_pair(packet->source, source_port));
  }

  const auto destination_mapping_it =
      switching_table_.find(packet->destination);
  if (destination_mapping_it != switching_table_.end()) {
    destination_mapping_it->second->EnqueuePacket(std::move(packet));
    return;
  }

  // If no mapping is available yet, broadcast the packet to all ports
  // different from the source.
  for (Port& egress_port : ports_) {
    if (!egress_port.connected()) {
      continue;
    }
    egress_port.EnqueuePacket(std::make_unique<Packet>(*packet));
  }
}

}  // namespace simulator
}  // namespace quic
