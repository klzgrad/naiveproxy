// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/quartc_competing_endpoint.h"

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {
namespace test {

QuartcCompetingEndpoint::QuartcCompetingEndpoint(
    simulator::Simulator* simulator,
    QuicTime::Delta send_interval,
    QuicByteCount bytes_per_interval,
    const std::string& name,
    const std::string& peer_name,
    Perspective perspective,
    QuicConnectionId connection_id)
    : Actor(simulator, QuicStrCat(name, " actor")),
      send_interval_(send_interval),
      bytes_per_interval_(bytes_per_interval),
      endpoint_(std::make_unique<simulator::QuicEndpoint>(simulator,
                                                          name,
                                                          peer_name,
                                                          perspective,
                                                          connection_id)) {
  // Schedule the first send for one send interval into the test.
  Schedule(simulator_->GetClock()->Now() + send_interval_);
  last_send_time_ = simulator_->GetClock()->Now();
}

void QuartcCompetingEndpoint::Act() {
  endpoint_->AddBytesToTransfer(bytes_per_interval_);
  if (send_interval_ > QuicTime::Delta::Zero()) {
    Schedule(last_send_time_ + send_interval_);
  }
  last_send_time_ = simulator_->GetClock()->Now();
}

}  // namespace test
}  // namespace quic
