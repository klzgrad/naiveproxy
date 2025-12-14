// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simulator/test_harness.h"

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/simulator/packet_filter.h"
#include "quiche/quic/test_tools/simulator/port.h"
#include "quiche/quic/test_tools/simulator/quic_endpoint_base.h"
#include "quiche/quic/test_tools/simulator/simulator.h"

namespace quic::simulator {

class LoseEveryNFilter : public PacketFilter {
 public:
  LoseEveryNFilter(Endpoint* input, int n)
      : PacketFilter(input->simulator(),
                     absl::StrCat(input->name(), " (loss filter)"), input),
        n_(n) {}

 protected:
  bool FilterPacket(const Packet& /*packet*/) {
    ++counter_;
    return (counter_ % n_) != 0;
  }

 private:
  int n_;
  int counter_ = 0;
};

QuicEndpointWithConnection::QuicEndpointWithConnection(
    Simulator* simulator, const std::string& name, const std::string& peer_name,
    Perspective perspective, const ParsedQuicVersionVector& supported_versions)
    : QuicEndpointBase(simulator, name, peer_name) {
  connection_ = std::make_unique<QuicConnection>(
      quic::test::TestConnectionId(0x10), GetAddressFromName(name),
      GetAddressFromName(peer_name), simulator, simulator->GetAlarmFactory(),
      &writer_, /*owns_writer=*/false, perspective, supported_versions,
      connection_id_generator_);
  connection_->SetSelfAddress(GetAddressFromName(name));
}

TestHarness::TestHarness() : switch_(&simulator_, "Switch", 8, 2 * kBdp) {}

void TestHarness::WireUpEndpoints() {
  client_link_.emplace(client_, switch_.port(1), kClientBandwidth,
                       kClientPropagationDelay);
  server_link_.emplace(server_, switch_.port(2), kServerBandwidth,
                       kServerPropagationDelay);
}

void TestHarness::WireUpEndpointsWithLoss(int lose_every_n) {
  client_filter_ = std::make_unique<LoseEveryNFilter>(client_, lose_every_n);
  server_filter_ = std::make_unique<LoseEveryNFilter>(server_, lose_every_n);
  client_link_.emplace(client_filter_.get(), switch_.port(1), kClientBandwidth,
                       kClientPropagationDelay);
  server_link_.emplace(server_filter_.get(), switch_.port(2), kServerBandwidth,
                       kServerPropagationDelay);
}

}  // namespace quic::simulator
