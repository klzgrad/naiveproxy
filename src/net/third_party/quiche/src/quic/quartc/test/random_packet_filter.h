// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_TEST_RANDOM_PACKET_FILTER_H_
#define QUICHE_QUIC_QUARTC_TEST_RANDOM_PACKET_FILTER_H_

#include "net/third_party/quiche/src/quic/test_tools/simulator/packet_filter.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/port.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

// Packet filter which randomly drops packets.
class RandomPacketFilter : public PacketFilter {
 public:
  RandomPacketFilter(Simulator* simulator,
                     const std::string& name,
                     Endpoint* endpoint);

  void set_loss_percent(double loss_percent) {
    DCHECK_GE(loss_percent, 0);
    DCHECK_LE(loss_percent, 100);
    loss_percent_ = loss_percent;
  }

 protected:
  bool FilterPacket(const Packet& packet) override;

 private:
  Simulator* simulator_;
  double loss_percent_ = 0;
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_TEST_RANDOM_PACKET_FILTER_H_
