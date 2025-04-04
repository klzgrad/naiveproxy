// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMULATOR_SWITCH_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMULATOR_SWITCH_H_

#include <deque>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/test_tools/simulator/queue.h"

namespace quic {
namespace simulator {

using SwitchPortNumber = size_t;

// Simulates a network switch with simple persistent learning scheme and queues
// on every output port.
class Switch {
 public:
  Switch(Simulator* simulator, std::string name, SwitchPortNumber port_count,
         QuicByteCount queue_capacity);
  Switch(const Switch&) = delete;
  Switch& operator=(const Switch&) = delete;
  ~Switch();

  // Returns Endpoint associated with the port under number |port_number|.  Just
  // like on most real switches, port numbering starts with 1.
  Endpoint* port(SwitchPortNumber port_number) {
    QUICHE_DCHECK_NE(port_number, 0u);
    return &ports_[port_number - 1];
  }

  Queue* port_queue(SwitchPortNumber port_number) {
    return ports_[port_number - 1].queue();
  }

 private:
  class Port : public Endpoint, public UnconstrainedPortInterface {
   public:
    Port(Simulator* simulator, std::string name, Switch* parent,
         SwitchPortNumber port_number, QuicByteCount queue_capacity);
    Port(Port&&) = delete;
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
    ~Port() override {}

    // Accepts packet to be routed into the switch.
    void AcceptPacket(std::unique_ptr<Packet> packet) override;
    // Enqueue packet to be routed out of the switch.
    void EnqueuePacket(std::unique_ptr<Packet> packet);

    UnconstrainedPortInterface* GetRxPort() override;
    void SetTxPort(ConstrainedPortInterface* port) override;

    void Act() override;

    bool connected() const { return connected_; }
    Queue* queue() { return &queue_; }

   private:
    Switch* parent_;
    SwitchPortNumber port_number_;
    bool connected_;

    Queue queue_;
  };

  // Sends the packet to the appropriate port, or to all ports if the
  // appropriate port is not known.
  void DispatchPacket(SwitchPortNumber port_number,
                      std::unique_ptr<Packet> packet);

  // This cannot be a quiche::QuicheCircularDeque since pointers into this are
  // assumed to be stable.
  std::deque<Port> ports_;
  absl::flat_hash_map<std::string, Port*> switching_table_;
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMULATOR_SWITCH_H_
