// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simulator/link.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "quiche/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

// Parameters for random noise delay.
const uint64_t kMaxRandomDelayUs = 10;

OneWayLink::OneWayLink(Simulator* simulator, std::string name,
                       UnconstrainedPortInterface* sink,
                       QuicBandwidth bandwidth,
                       QuicTime::Delta propagation_delay)
    : Actor(simulator, name),
      sink_(sink),
      bandwidth_(bandwidth),
      propagation_delay_(propagation_delay),
      next_write_at_(QuicTime::Zero()) {}

OneWayLink::~OneWayLink() {}

OneWayLink::QueuedPacket::QueuedPacket(std::unique_ptr<Packet> packet,
                                       QuicTime dequeue_time)
    : packet(std::move(packet)), dequeue_time(dequeue_time) {}

OneWayLink::QueuedPacket::QueuedPacket(QueuedPacket&& other) = default;

OneWayLink::QueuedPacket::~QueuedPacket() {}

void OneWayLink::AcceptPacket(std::unique_ptr<Packet> packet) {
  QUICHE_DCHECK(TimeUntilAvailable().IsZero());
  QuicTime::Delta transfer_time = bandwidth_.TransferTime(packet->size);
  next_write_at_ = clock_->Now() + transfer_time;

  packets_in_transit_.emplace_back(
      std::move(packet),
      // Ensure that packets are delivered in order.
      std::max(
          next_write_at_ + propagation_delay_ + GetRandomDelay(transfer_time),
          packets_in_transit_.empty()
              ? QuicTime::Zero()
              : packets_in_transit_.back().dequeue_time));
  ScheduleNextPacketDeparture();
}

QuicTime::Delta OneWayLink::TimeUntilAvailable() {
  const QuicTime now = clock_->Now();
  if (next_write_at_ <= now) {
    return QuicTime::Delta::Zero();
  }

  return next_write_at_ - now;
}

void OneWayLink::Act() {
  QUICHE_DCHECK(!packets_in_transit_.empty());
  QUICHE_DCHECK(packets_in_transit_.front().dequeue_time >= clock_->Now());

  sink_->AcceptPacket(std::move(packets_in_transit_.front().packet));
  packets_in_transit_.pop_front();

  ScheduleNextPacketDeparture();
}

void OneWayLink::ScheduleNextPacketDeparture() {
  if (packets_in_transit_.empty()) {
    return;
  }

  Schedule(packets_in_transit_.front().dequeue_time);
}

QuicTime::Delta OneWayLink::GetRandomDelay(QuicTime::Delta transfer_time) {
  if (!simulator_->enable_random_delays()) {
    return QuicTime::Delta::Zero();
  }

  QuicTime::Delta delta = QuicTime::Delta::FromMicroseconds(
      simulator_->GetRandomGenerator()->RandUint64() % (kMaxRandomDelayUs + 1));
  // Have an upper bound on the delay to ensure packets do not go out of order.
  delta = std::min(delta, transfer_time * 0.5);
  return delta;
}

SymmetricLink::SymmetricLink(Simulator* simulator, std::string name,
                             UnconstrainedPortInterface* sink_a,
                             UnconstrainedPortInterface* sink_b,
                             QuicBandwidth bandwidth,
                             QuicTime::Delta propagation_delay)
    : a_to_b_link_(simulator, absl::StrCat(name, " (A-to-B)"), sink_b,
                   bandwidth, propagation_delay),
      b_to_a_link_(simulator, absl::StrCat(name, " (B-to-A)"), sink_a,
                   bandwidth, propagation_delay) {}

SymmetricLink::SymmetricLink(Endpoint* endpoint_a, Endpoint* endpoint_b,
                             QuicBandwidth bandwidth,
                             QuicTime::Delta propagation_delay)
    : SymmetricLink(endpoint_a->simulator(),
                    absl::StrFormat("Link [%s]<->[%s]", endpoint_a->name(),
                                    endpoint_b->name()),
                    endpoint_a->GetRxPort(), endpoint_b->GetRxPort(), bandwidth,
                    propagation_delay) {
  endpoint_a->SetTxPort(&a_to_b_link_);
  endpoint_b->SetTxPort(&b_to_a_link_);
}

}  // namespace simulator
}  // namespace quic
