// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMULATOR_TEST_HARNESS_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMULATOR_TEST_HARNESS_H_

#include <memory>
#include <optional>
#include <string>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/test_tools/simulator/link.h"
#include "quiche/quic/test_tools/simulator/packet_filter.h"
#include "quiche/quic/test_tools/simulator/port.h"
#include "quiche/quic/test_tools/simulator/quic_endpoint_base.h"
#include "quiche/quic/test_tools/simulator/simulator.h"
#include "quiche/quic/test_tools/simulator/switch.h"

namespace quic::simulator {

// A subclass of QuicEndpointBase that creates the connection object for the
// caller.  Uses a fixed connection ID (0x10) and IP addresses derived from the
// names supplied.
class QuicEndpointWithConnection : public QuicEndpointBase {
 public:
  QuicEndpointWithConnection(Simulator* simulator, const std::string& name,
                             const std::string& peer_name,
                             Perspective perspective,
                             const ParsedQuicVersionVector& supported_versions);
};

// A test harness that provides a reasonable preset for running unit tests.
class TestHarness {
 public:
  // The configuration of the test harness.
  static constexpr QuicBandwidth kClientBandwidth =
      QuicBandwidth::FromKBitsPerSecond(10000);
  static constexpr QuicTime::Delta kClientPropagationDelay =
      QuicTime::Delta::FromMilliseconds(2);
  static constexpr QuicBandwidth kServerBandwidth =
      QuicBandwidth::FromKBitsPerSecond(4000);
  static constexpr QuicTime::Delta kServerPropagationDelay =
      QuicTime::Delta::FromMilliseconds(50);
  static constexpr QuicTime::Delta kTransferTime =
      kClientBandwidth.TransferTime(kMaxOutgoingPacketSize) +
      kServerBandwidth.TransferTime(kMaxOutgoingPacketSize);
  static constexpr QuicTime::Delta kRtt =
      (kClientPropagationDelay + kServerPropagationDelay + kTransferTime) * 2;
  static constexpr QuicByteCount kBdp = kRtt * kServerBandwidth;

  static constexpr QuicTime::Delta kDefaultTimeout =
      QuicTime::Delta::FromSeconds(3);

  TestHarness();

  Simulator& simulator() { return simulator_; }
  void set_client(Endpoint* client) { client_ = client; }
  void set_server(Endpoint* server) { server_ = server; }

  // Connects |client_| and |server_| to a virtual switch; must be called after
  // set_client/set_server are called.
  void WireUpEndpoints();

  // Same as above, except triggers loss of every Nth packet in both directions.
  void WireUpEndpointsWithLoss(int lose_every_n);

  // A convenience wrapper around Simulator::RunUntilOrTimeout().
  template <class TerminationPredicate>
  bool RunUntilWithDefaultTimeout(TerminationPredicate termination_predicate) {
    return simulator_.RunUntilOrTimeout(std::move(termination_predicate),
                                        kDefaultTimeout);
  }

 private:
  Simulator simulator_;
  Switch switch_;
  std::optional<SymmetricLink> client_link_;
  std::optional<SymmetricLink> server_link_;
  std::unique_ptr<PacketFilter> client_filter_;
  std::unique_ptr<PacketFilter> server_filter_;

  Endpoint* client_;
  Endpoint* server_;
};

}  // namespace quic::simulator

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMULATOR_TEST_HARNESS_H_
