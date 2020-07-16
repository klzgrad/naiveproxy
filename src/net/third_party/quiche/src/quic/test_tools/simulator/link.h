// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMULATOR_LINK_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMULATOR_LINK_H_

#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/actor.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/port.h"

namespace quic {
namespace simulator {

// A reliable simplex link between two endpoints with constrained bandwidth.  A
// few microseconds of random delay are added for every packet to avoid
// synchronization issues.
class OneWayLink : public Actor, public ConstrainedPortInterface {
 public:
  OneWayLink(Simulator* simulator,
             std::string name,
             UnconstrainedPortInterface* sink,
             QuicBandwidth bandwidth,
             QuicTime::Delta propagation_delay);
  OneWayLink(const OneWayLink&) = delete;
  OneWayLink& operator=(const OneWayLink&) = delete;
  ~OneWayLink() override;

  void AcceptPacket(std::unique_ptr<Packet> packet) override;
  QuicTime::Delta TimeUntilAvailable() override;
  void Act() override;

  inline QuicBandwidth bandwidth() const { return bandwidth_; }
  inline void set_bandwidth(QuicBandwidth new_bandwidth) {
    bandwidth_ = new_bandwidth;
  }

 protected:
  // Get the value of a random delay imposed on each packet.  By default, this
  // is a short random delay in order to avoid artifical synchronization
  // artifacts during the simulation.  Subclasses may override this behavior
  // (for example, to provide a random component of delay).
  virtual QuicTime::Delta GetRandomDelay(QuicTime::Delta transfer_time);

 private:
  struct QueuedPacket {
    std::unique_ptr<Packet> packet;
    QuicTime dequeue_time;

    QueuedPacket(std::unique_ptr<Packet> packet, QuicTime dequeue_time);
    QueuedPacket(QueuedPacket&& other);
    ~QueuedPacket();
  };

  // Schedule the next packet to be egressed out of the link if there are
  // packets on the link.
  void ScheduleNextPacketDeparture();

  UnconstrainedPortInterface* sink_;
  QuicCircularDeque<QueuedPacket> packets_in_transit_;

  QuicBandwidth bandwidth_;
  const QuicTime::Delta propagation_delay_;

  QuicTime next_write_at_;
};

// A full-duplex link between two endpoints, functionally equivalent to two
// OneWayLink objects tied together.
class SymmetricLink {
 public:
  SymmetricLink(Simulator* simulator,
                std::string name,
                UnconstrainedPortInterface* sink_a,
                UnconstrainedPortInterface* sink_b,
                QuicBandwidth bandwidth,
                QuicTime::Delta propagation_delay);
  SymmetricLink(Endpoint* endpoint_a,
                Endpoint* endpoint_b,
                QuicBandwidth bandwidth,
                QuicTime::Delta propagation_delay);
  SymmetricLink(const SymmetricLink&) = delete;
  SymmetricLink& operator=(const SymmetricLink&) = delete;

  inline QuicBandwidth bandwidth() { return a_to_b_link_.bandwidth(); }
  inline void set_bandwidth(QuicBandwidth new_bandwidth) {
    a_to_b_link_.set_bandwidth(new_bandwidth);
    b_to_a_link_.set_bandwidth(new_bandwidth);
  }

 private:
  OneWayLink a_to_b_link_;
  OneWayLink b_to_a_link_;
};

}  // namespace simulator
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMULATOR_LINK_H_
