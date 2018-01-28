// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/test_tools/simulator/simulator.h"

namespace net {
namespace simulator {

Simulator::Simulator()
    : random_generator_(nullptr),
      alarm_factory_(this, "Default Alarm Manager"),
      run_for_should_stop_(false),
      enable_random_delays_(false) {
  run_for_alarm_.reset(
      alarm_factory_.CreateAlarm(new RunForDelegate(&run_for_should_stop_)));
}

Simulator::~Simulator() {}

Simulator::Clock::Clock() : now_(kStartTime) {}

QuicTime Simulator::Clock::ApproximateNow() const {
  return now_;
}

QuicTime Simulator::Clock::Now() const {
  return now_;
}

QuicWallTime Simulator::Clock::WallNow() const {
  return QuicWallTime::FromUNIXMicroseconds(
      (now_ - QuicTime::Zero()).ToMicroseconds());
}

void Simulator::AddActor(Actor* actor) {
  auto emplace_times_result =
      scheduled_times_.insert(std::make_pair(actor, QuicTime::Infinite()));
  auto emplace_names_result = actor_names_.insert(actor->name());

  // Ensure that the object was actually placed into the map.
  DCHECK(emplace_times_result.second);
  DCHECK(emplace_names_result.second);
}

void Simulator::Schedule(Actor* actor, QuicTime new_time) {
  auto scheduled_time_it = scheduled_times_.find(actor);
  DCHECK(scheduled_time_it != scheduled_times_.end());
  QuicTime scheduled_time = scheduled_time_it->second;

  if (scheduled_time <= new_time) {
    return;
  }

  if (scheduled_time != QuicTime::Infinite()) {
    Unschedule(actor);
  }

  scheduled_time_it->second = new_time;
  schedule_.insert(std::make_pair(new_time, actor));
}

void Simulator::Unschedule(Actor* actor) {
  auto scheduled_time_it = scheduled_times_.find(actor);
  DCHECK(scheduled_time_it != scheduled_times_.end());
  QuicTime scheduled_time = scheduled_time_it->second;

  DCHECK(scheduled_time != QuicTime::Infinite());
  auto range = schedule_.equal_range(scheduled_time);
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second == actor) {
      schedule_.erase(it);
      scheduled_time_it->second = QuicTime::Infinite();
      return;
    }
  }
  DCHECK(false);
}

const QuicClock* Simulator::GetClock() const {
  return &clock_;
}

QuicRandom* Simulator::GetRandomGenerator() {
  if (random_generator_ == nullptr) {
    random_generator_ = QuicRandom::GetInstance();
  }

  return random_generator_;
}

QuicBufferAllocator* Simulator::GetStreamSendBufferAllocator() {
  return &buffer_allocator_;
}

QuicAlarmFactory* Simulator::GetAlarmFactory() {
  return &alarm_factory_;
}

Simulator::RunForDelegate::RunForDelegate(bool* run_for_should_stop)
    : run_for_should_stop_(run_for_should_stop) {}

void Simulator::RunForDelegate::OnAlarm() {
  *run_for_should_stop_ = true;
}

void Simulator::RunFor(QuicTime::Delta time_span) {
  DCHECK(!run_for_alarm_->IsSet());

  // RunFor() ensures that the simulation stops at the exact time specified by
  // scheduling an alarm at that point and using that alarm to abort the
  // simulation.  An alarm is necessary because otherwise it is possible that
  // nothing is scheduled at |end_time|, so the simulation will either go
  // further than requested or stop before reaching |end_time|.
  const QuicTime end_time = clock_.Now() + time_span;
  run_for_alarm_->Set(end_time);
  run_for_should_stop_ = false;
  bool simulation_result = RunUntil([this]() { return run_for_should_stop_; });

  DCHECK(simulation_result);
  DCHECK(clock_.Now() == end_time);
}

void Simulator::HandleNextScheduledActor() {
  const auto current_event_it = schedule_.begin();
  QuicTime event_time = current_event_it->first;
  Actor* actor = current_event_it->second;
  QUIC_DVLOG(3) << "At t = " << event_time.ToDebuggingValue() << ", calling "
                << actor->name();

  Unschedule(actor);

  if (clock_.Now() > event_time) {
    QUIC_BUG << "Error: event registered by [" << actor->name()
             << "] requires travelling back in time.  Current time: "
             << clock_.Now().ToDebuggingValue()
             << ", scheduled time: " << event_time.ToDebuggingValue();
  }
  clock_.now_ = event_time;

  actor->Act();
}

}  // namespace simulator
}  // namespace net
