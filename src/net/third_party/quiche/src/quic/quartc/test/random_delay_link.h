// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_TEST_RANDOM_DELAY_LINK_H_
#define QUICHE_QUIC_QUARTC_TEST_RANDOM_DELAY_LINK_H_

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/port.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

// A reliable simplex link between two endpoints with constrained bandwidth.  A
// random delay is added to each packet.  The random values are chosen
// separately for each packet, following an exponential distribution.
class RandomDelayLink : public OneWayLink {
 public:
  RandomDelayLink(Simulator* simulator,
                  std::string name,
                  UnconstrainedPortInterface* sink,
                  QuicBandwidth bandwidth,
                  QuicTime::Delta propagation_delay);
  RandomDelayLink(const RandomDelayLink&) = delete;
  RandomDelayLink& operator=(const RandomDelayLink&) = delete;
  ~RandomDelayLink() override;

  // Sets the median value of the random delay introduced by this link.  Random
  // delays are chosen according to an exponential distribution, clipped and
  // scaled to reach this as a median value.
  inline void set_median_random_delay(QuicTime::Delta delta) {
    median_random_delay_ = delta;
  }

 protected:
  QuicTime::Delta GetRandomDelay(QuicTime::Delta transfer_time) override;

 private:
  QuicTime::Delta median_random_delay_;
};

// A full-duplex link between two endpoints, functionally equivalent to two
// RandomDelayLink objects tied together.
class SymmetricRandomDelayLink {
 public:
  SymmetricRandomDelayLink(Simulator* simulator,
                           std::string name,
                           UnconstrainedPortInterface* sink_a,
                           UnconstrainedPortInterface* sink_b,
                           QuicBandwidth bandwidth,
                           QuicTime::Delta propagation_delay);
  SymmetricRandomDelayLink(Endpoint* endpoint_a,
                           Endpoint* endpoint_b,
                           QuicBandwidth bandwidth,
                           QuicTime::Delta propagation_delay);
  SymmetricRandomDelayLink(const SymmetricRandomDelayLink&) = delete;
  SymmetricRandomDelayLink& operator=(const SymmetricRandomDelayLink&) = delete;

  inline QuicBandwidth bandwidth() { return a_to_b_link_.bandwidth(); }

  inline void set_median_random_delay(QuicTime::Delta delay) {
    a_to_b_link_.set_median_random_delay(delay);
    b_to_a_link_.set_median_random_delay(delay);
  }

 private:
  RandomDelayLink a_to_b_link_;
  RandomDelayLink b_to_a_link_;
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_TEST_RANDOM_DELAY_LINK_H_
