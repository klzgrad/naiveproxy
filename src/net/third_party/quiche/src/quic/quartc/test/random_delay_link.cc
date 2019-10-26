// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/random_delay_link.h"

#include <cmath>
#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/port.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {
namespace {

// Number of buckets used to define an exponential distribution.
constexpr int64_t kNumBuckets = static_cast<int64_t>(2) << 32;

}  // namespace

RandomDelayLink::RandomDelayLink(Simulator* simulator,
                                 std::string name,
                                 UnconstrainedPortInterface* sink,
                                 QuicBandwidth bandwidth,
                                 QuicTime::Delta propagation_delay)
    : OneWayLink(simulator, name, sink, bandwidth, propagation_delay),
      median_random_delay_(QuicTime::Delta::Zero()) {}

RandomDelayLink::~RandomDelayLink() {}

QuicTime::Delta RandomDelayLink::GetRandomDelay(
    QuicTime::Delta /*transfer_time*/) {
  // Computes a random delay following an exponential distribution, with median
  // value |median_random_delay_|.  Choose a uniform random value between 1 and
  // kNumBuckets, convert this to an exponential, then scale it such that a
  // random value from the middle of the distribution (0.5) corresponds to
  // |median_random_delay_|.
  return std::log(
             static_cast<double>(
                 simulator_->GetRandomGenerator()->RandUint64() % kNumBuckets +
                 1) /
             kNumBuckets) /
         std::log(0.5) * median_random_delay_;
}

SymmetricRandomDelayLink::SymmetricRandomDelayLink(
    Simulator* simulator,
    std::string name,
    UnconstrainedPortInterface* sink_a,
    UnconstrainedPortInterface* sink_b,
    QuicBandwidth bandwidth,
    QuicTime::Delta propagation_delay)
    : a_to_b_link_(simulator,
                   QuicStringPrintf("%s (A-to-B)", name.c_str()),
                   sink_b,
                   bandwidth,
                   propagation_delay),
      b_to_a_link_(simulator,
                   QuicStringPrintf("%s (B-to-A)", name.c_str()),
                   sink_a,
                   bandwidth,
                   propagation_delay) {}

SymmetricRandomDelayLink::SymmetricRandomDelayLink(
    Endpoint* endpoint_a,
    Endpoint* endpoint_b,
    QuicBandwidth bandwidth,
    QuicTime::Delta propagation_delay)
    : SymmetricRandomDelayLink(endpoint_a->simulator(),
                               QuicStringPrintf("Link [%s]<->[%s]",
                                                endpoint_a->name().c_str(),
                                                endpoint_b->name().c_str()),
                               endpoint_a->GetRxPort(),
                               endpoint_b->GetRxPort(),
                               bandwidth,
                               propagation_delay) {
  endpoint_a->SetTxPort(&a_to_b_link_);
  endpoint_b->SetTxPort(&b_to_a_link_);
}

}  // namespace simulator
}  // namespace quic
