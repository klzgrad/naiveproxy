// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_ALARM_FACTORY_PROXY_H_
#define QUICHE_QUIC_CORE_QUIC_ALARM_FACTORY_PROXY_H_

#include <utility>

#include "absl/base/nullability.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_one_block_arena.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// QuicAlarmFactoryProxy passes calls to the specified unowned QuicAlarmFactory.
class QUICHE_EXPORT QuicAlarmFactoryProxy : public QuicAlarmFactory {
 public:
  explicit QuicAlarmFactoryProxy(QuicAlarmFactory* /*absl_nonnull*/ alarm_factory)
      : alarm_factory_(*alarm_factory) {}

  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override {
    return alarm_factory_.CreateAlarm(delegate);
  }
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override {
    return alarm_factory_.CreateAlarm(std::move(delegate), arena);
  }

 private:
  QuicAlarmFactory& alarm_factory_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_ALARM_FACTORY_PROXY_H_
