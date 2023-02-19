// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simulator/test_harness.h"

#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/simulator/quic_endpoint_base.h"

namespace quic::simulator {

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

}  // namespace quic::simulator
