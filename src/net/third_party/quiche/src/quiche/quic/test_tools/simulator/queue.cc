// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simulator/queue.h"

#include <memory>
#include <string>
#include <utility>

#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

Queue::ListenerInterface::~ListenerInterface() {}

Queue::Queue(Simulator* simulator, std::string name, QuicByteCount capacity)
    : Actor(simulator, name),
      capacity_(capacity),
      bytes_queued_(0),
      aggregation_threshold_(0),
      aggregation_timeout_(QuicTime::Delta::Infinite()),
      current_bundle_(0),
      current_bundle_bytes_(0),
      tx_port_(nullptr),
      listener_(nullptr) {
  aggregation_timeout_alarm_.reset(simulator_->GetAlarmFactory()->CreateAlarm(
      new AggregationAlarmDelegate(this)));
}

Queue::~Queue() { aggregation_timeout_alarm_->PermanentCancel(); }

void Queue::set_tx_port(ConstrainedPortInterface* port) { tx_port_ = port; }

void Queue::AcceptPacket(std::unique_ptr<Packet> packet) {
  if (packet->size + bytes_queued_ > capacity_) {
    QUIC_DVLOG(1) << "Queue [" << name() << "] has received a packet from ["
                  << packet->source << "] to [" << packet->destination
                  << "] which is over capacity.  Dropping it.";
    QUIC_DVLOG(1) << "Queue size: " << bytes_queued_ << " out of " << capacity_
                  << ".  Packet size: " << packet->size;
    return;
  }

  bytes_queued_ += packet->size;
  queue_.emplace_back(std::move(packet), current_bundle_);

  if (IsAggregationEnabled()) {
    current_bundle_bytes_ += queue_.front().packet->size;
    if (!aggregation_timeout_alarm_->IsSet()) {
      aggregation_timeout_alarm_->Set(clock_->Now() + aggregation_timeout_);
    }
    if (current_bundle_bytes_ >= aggregation_threshold_) {
      NextBundle();
    }
  }

  ScheduleNextPacketDequeue();
}

void Queue::Act() {
  QUICHE_DCHECK(!queue_.empty());
  if (tx_port_->TimeUntilAvailable().IsZero()) {
    QUICHE_DCHECK(bytes_queued_ >= queue_.front().packet->size);
    bytes_queued_ -= queue_.front().packet->size;

    tx_port_->AcceptPacket(std::move(queue_.front().packet));
    queue_.pop_front();
    if (listener_ != nullptr) {
      listener_->OnPacketDequeued();
    }
  }

  ScheduleNextPacketDequeue();
}

void Queue::EnableAggregation(QuicByteCount aggregation_threshold,
                              QuicTime::Delta aggregation_timeout) {
  QUICHE_DCHECK_EQ(bytes_queued_, 0u);
  QUICHE_DCHECK_GT(aggregation_threshold, 0u);
  QUICHE_DCHECK(!aggregation_timeout.IsZero());
  QUICHE_DCHECK(!aggregation_timeout.IsInfinite());

  aggregation_threshold_ = aggregation_threshold;
  aggregation_timeout_ = aggregation_timeout;
}

Queue::AggregationAlarmDelegate::AggregationAlarmDelegate(Queue* queue)
    : queue_(queue) {}

void Queue::AggregationAlarmDelegate::OnAlarm() {
  queue_->NextBundle();
  queue_->ScheduleNextPacketDequeue();
}

Queue::EnqueuedPacket::EnqueuedPacket(std::unique_ptr<Packet> packet,
                                      AggregationBundleNumber bundle)
    : packet(std::move(packet)), bundle(bundle) {}

Queue::EnqueuedPacket::EnqueuedPacket(EnqueuedPacket&& other) = default;

Queue::EnqueuedPacket::~EnqueuedPacket() = default;

void Queue::NextBundle() {
  current_bundle_++;
  current_bundle_bytes_ = 0;
  aggregation_timeout_alarm_->Cancel();
}

void Queue::ScheduleNextPacketDequeue() {
  if (queue_.empty()) {
    QUICHE_DCHECK_EQ(bytes_queued_, 0u);
    return;
  }

  if (IsAggregationEnabled() && queue_.front().bundle == current_bundle_) {
    return;
  }

  QuicTime::Delta time_until_available = QuicTime::Delta::Zero();
  if (tx_port_) {
    time_until_available = tx_port_->TimeUntilAvailable();
  }

  Schedule(clock_->Now() + time_until_available);
}

}  // namespace simulator
}  // namespace quic
