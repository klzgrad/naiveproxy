// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_TEST_QUARTC_COMPETING_ENDPOINT_H_
#define QUICHE_QUIC_QUARTC_TEST_QUARTC_COMPETING_ENDPOINT_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/actor.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace test {

// Wrapper for a QUIC endpoint that competes with a Quartc flow in simulator
// tests.  A competing endpoint sends a fixed number of bytes at a fixed
// frequency.
class QuartcCompetingEndpoint : public simulator::Actor {
 public:
  // Creates a competing endpoint that sends |bytes_per_interval| every
  // |send_interval|, starting one |send_interval| after it is created
  // (according to |simulator|'s clock).
  QuartcCompetingEndpoint(simulator::Simulator* simulator,
                          QuicTime::Delta send_interval,
                          QuicByteCount bytes_per_interval,
                          const std::string& name,
                          const std::string& peer_name,
                          Perspective perspective,
                          QuicConnectionId connection_id);

  simulator::QuicEndpoint* endpoint() { return endpoint_.get(); }

  void Act() override;

 private:
  const QuicTime::Delta send_interval_;
  const QuicByteCount bytes_per_interval_;
  std::unique_ptr<simulator::QuicEndpoint> endpoint_;
  std::unique_ptr<QuicAlarm> send_alarm_;

  QuicTime last_send_time_ = QuicTime::Zero();
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_TEST_QUARTC_COMPETING_ENDPOINT_H_
