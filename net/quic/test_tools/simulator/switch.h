// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_SIMULATOR_SWITCH_H_
#define NET_QUIC_TEST_TOOLS_SIMULATOR_SWITCH_H_

#include <deque>

#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/test_tools/simulator/queue.h"

namespace net {
namespace simulator {

typedef size_t SwitchPortNumber;

// Simulates a network switch with simple persistent learning scheme and queues
// on every output port.
class Switch {
 public:
  Switch(Simulator* simulator,
         std::string name,
         SwitchPortNumber port_count,
         QuicByteCount queue_capacity);
  ~Switch();

  // Returns Endpoint associated with the port under number |port_number|.  Just
  // like on most real switches, port numbering starts with 1.
  inline Endpoint* port(SwitchPortNumber port_number) {
    DCHECK_NE(port_number, 0u);
    return &ports_[port_number - 1];
  }

  inline const Queue* port_queue(SwitchPortNumber port_number) const {
    return ports_[port_number - 1].queue();
  }
  inline Queue* port_queue(SwitchPortNumber port_number) {
    return ports_[port_number - 1].queue();
  }

 private:
  class Port : public Endpoint, public UnconstrainedPortInterface {
   public:
    Port(Simulator* simulator,
         std::string name,
         Switch* parent,
         SwitchPortNumber port_number,
         QuicByteCount queue_capacity);
    Port(Port&&) = delete;
    ~Port() override {}

    // Accepts packet to be routed into the switch.
    void AcceptPacket(std::unique_ptr<Packet> packet) override;
    // Enqueue packet to be routed out of the switch.
    void EnqueuePacket(std::unique_ptr<Packet> packet);

    UnconstrainedPortInterface* GetRxPort() override;
    void SetTxPort(ConstrainedPortInterface* port) override;

    void Act() override;

    inline bool connected() const { return connected_; }
    inline const Queue* queue() const { return &queue_; }
    inline Queue* queue() { return &queue_; }

   private:
    Switch* parent_;
    SwitchPortNumber port_number_;
    bool connected_;

    Queue queue_;

    DISALLOW_COPY_AND_ASSIGN(Port);
  };

  // Sends the packet to the appropriate port, or to all ports if the
  // appropriate port is not known.
  void DispatchPacket(SwitchPortNumber port_number,
                      std::unique_ptr<Packet> packet);

  // This can not be a QuicDeque since pointers into this are
  // assumed to be stable.
  std::deque<Port> ports_;
  QuicUnorderedMap<std::string, Port*> switching_table_;

  DISALLOW_COPY_AND_ASSIGN(Switch);
};

}  // namespace simulator
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_SIMULATOR_SWITCH_H_
