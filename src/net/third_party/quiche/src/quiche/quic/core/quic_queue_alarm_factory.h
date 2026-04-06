// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_QUEUE_ALARM_FACTORY_H_
#define QUICHE_QUIC_CORE_QUIC_QUEUE_ALARM_FACTORY_H_

#include <memory>
#include <optional>

#include "absl/container/btree_map.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// QuicQueueAlarmFactory maintains a queue of scheduled alarms internally, and
// provides methods to query the time of the next alarm and to execute all of
// the ones that are past the deadline.
class QUICHE_EXPORT QuicQueueAlarmFactory : public QuicAlarmFactory {
 public:
  // Calls all of the alarm callbacks that are scheduled before or at |time|.
  void ProcessAlarmsUpTo(QuicTime time);

  // Returns the deadline of the next upcoming alarm, if any are scheduled.
  std::optional<QuicTime> GetNextUpcomingAlarm() const;

  // QuicAlarmFactory implementation.
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

 private:
  class Alarm : public QuicAlarm {
   public:
    Alarm(QuicQueueAlarmFactory* factory,
          QuicArenaScopedPtr<QuicAlarm::Delegate> delegate);

    void SetImpl() override;
    void CancelImpl() override;

    void DoFire() {
      current_schedule_handle_.reset();
      Fire();
    }

   private:
    QuicQueueAlarmFactory* factory_;

    // Deleted when the alarm is cancelled, causing the corresponding weak_ptr
    // in the alarm list to not be executed.
    std::shared_ptr<Alarm*> current_schedule_handle_;
  };

  // Alarms are stored as weak pointers, since the alarm can be cancelled and
  // disappear while in the queue.
  using AlarmList = absl::btree_multimap<QuicTime, std::weak_ptr<Alarm*>>;
  AlarmList alarms_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_QUEUE_ALARM_FACTORY_H_
