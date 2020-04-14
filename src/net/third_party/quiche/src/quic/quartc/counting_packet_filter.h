// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_COUNTING_PACKET_FILTER_H_
#define QUICHE_QUIC_QUARTC_COUNTING_PACKET_FILTER_H_

#include <string>

#include "net/third_party/quiche/src/quic/test_tools/simulator/packet_filter.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/port.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

// Simple packet filter which drops the first N packets it observes.
class CountingPacketFilter : public simulator::PacketFilter {
 public:
  CountingPacketFilter(simulator::Simulator* simulator,
                       const std::string& name,
                       simulator::Endpoint* endpoint)
      : PacketFilter(simulator, name, endpoint) {}

  void set_packets_to_drop(int count) { packets_to_drop_ = count; }

 protected:
  bool FilterPacket(const simulator::Packet& /*packet*/) override {
    if (packets_to_drop_ > 0) {
      --packets_to_drop_;
      return false;
    }
    return true;
  }

 private:
  int packets_to_drop_ = 0;
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_COUNTING_PACKET_FILTER_H_
