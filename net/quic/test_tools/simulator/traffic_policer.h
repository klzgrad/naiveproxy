// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef NET_QUIC_TEST_TOOLS_SIMULATOR_TRAFFIC_POLICER_H_
#define NET_QUIC_TEST_TOOLS_SIMULATOR_TRAFFIC_POLICER_H_

#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/test_tools/simulator/packet_filter.h"
#include "net/quic/test_tools/simulator/port.h"

namespace net {
namespace simulator {

// Traffic policer uses a token bucket to limit the bandwidth of the traffic
// passing through.  It wraps around an input port and exposes an output port.
// Only the traffic from input to the output is policed, so in case when
// bidirectional policing is desired, two policers have to be used.  The flows
// are hashed by the destination only.
class TrafficPolicer : public PacketFilter {
 public:
  TrafficPolicer(Simulator* simulator,
                 std::string name,
                 QuicByteCount initial_bucket_size,
                 QuicByteCount max_bucket_size,
                 QuicBandwidth target_bandwidth,
                 Endpoint* input);
  ~TrafficPolicer() override;

 protected:
  bool FilterPacket(const Packet& packet) override;

 private:
  // Refill the token buckets with all the tokens that have been granted since
  // |last_refill_time_|.
  void Refill();

  QuicByteCount initial_bucket_size_;
  QuicByteCount max_bucket_size_;
  QuicBandwidth target_bandwidth_;

  // The time at which the token buckets were last refilled.
  QuicTime last_refill_time_;

  // Maps each destination to the number of tokens it has left.
  QuicUnorderedMap<std::string, QuicByteCount> token_buckets_;

  DISALLOW_COPY_AND_ASSIGN(TrafficPolicer);
};

}  // namespace simulator
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_SIMULATOR_TRAFFIC_POLICER_H_
