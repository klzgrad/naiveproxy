// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_queue_alarm_factory.h"

#include <memory>
#include <utility>
#include <vector>

#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/quic/core/quic_time.h"

namespace quic {

void QuicQueueAlarmFactory::ProcessAlarmsUpTo(QuicTime time) {
  // Determine which alarm callbacks needs to be run.
  std::vector<std::weak_ptr<Alarm*>> alarms_to_call;
  while (!alarms_.empty() && alarms_.begin()->first <= time) {
    auto& [deadline, schedule_handle_weak] = *alarms_.begin();
    alarms_to_call.push_back(std::move(schedule_handle_weak));
    alarms_.erase(alarms_.begin());
  }
  // Actually run those callbacks.
  for (std::weak_ptr<Alarm*>& schedule_handle_weak : alarms_to_call) {
    std::shared_ptr<Alarm*> schedule_handle = schedule_handle_weak.lock();
    if (!schedule_handle) {
      // The alarm has been cancelled and might not even exist anymore.
      continue;
    }
    (*schedule_handle)->DoFire();
  }
  // Clean up all of the alarms in the front that have been cancelled.
  while (!alarms_.empty()) {
    if (alarms_.begin()->second.expired()) {
      alarms_.erase(alarms_.begin());
    } else {
      break;
    }
  }
}

QuicAlarm* QuicQueueAlarmFactory::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new Alarm(this, QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> QuicQueueAlarmFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<Alarm>(this, std::move(delegate));
  }
  return QuicArenaScopedPtr<QuicAlarm>(new Alarm(this, std::move(delegate)));
}

QuicQueueAlarmFactory::Alarm::Alarm(
    QuicQueueAlarmFactory* factory,
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
    : QuicAlarm(std::move(delegate)), factory_(factory) {}

void QuicQueueAlarmFactory::Alarm::SetImpl() {
  current_schedule_handle_ = std::make_shared<Alarm*>(this);
  factory_->alarms_.insert({deadline(), current_schedule_handle_});
}

void QuicQueueAlarmFactory::Alarm::CancelImpl() {
  current_schedule_handle_.reset();
}

std::optional<QuicTime> QuicQueueAlarmFactory::GetNextUpcomingAlarm() const {
  if (alarms_.empty()) {
    return std::nullopt;
  }
  return alarms_.begin()->first;
}

}  // namespace quic
