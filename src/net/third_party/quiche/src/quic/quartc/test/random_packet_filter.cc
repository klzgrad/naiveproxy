// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/random_packet_filter.h"

namespace quic {
namespace simulator {

RandomPacketFilter::RandomPacketFilter(Simulator* simulator,
                                       const std::string& name,
                                       Endpoint* endpoint)
    : PacketFilter(simulator, name, endpoint), simulator_(simulator) {}

bool RandomPacketFilter::FilterPacket(const Packet& /*packet*/) {
  uint64_t random = simulator_->GetRandomGenerator()->RandUint64();
  return 100 * static_cast<double>(random) /
             std::numeric_limits<uint64_t>::max() >=
         loss_percent_;
}

}  // namespace simulator
}  // namespace quic
