// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_alarm.h"

#include <atomic>

#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"

namespace quic {

QuicAlarm::QuicAlarm(QuicArenaScopedPtr<Delegate> delegate)
    : delegate_(std::move(delegate)), deadline_(QuicTime::Zero()) {}

QuicAlarm::~QuicAlarm() {
  if (IsSet()) {
    QUIC_CODE_COUNT(quic_alarm_not_cancelled_in_dtor);
  }
}

void QuicAlarm::Set(QuicTime new_deadline) {
  QUICHE_DCHECK(!IsSet());
  QUICHE_DCHECK(new_deadline.IsInitialized());

  if (IsPermanentlyCancelled()) {
    QUIC_BUG(quic_alarm_illegal_set)
        << "Set called after alarm is permanently cancelled. new_deadline:"
        << new_deadline;
    return;
  }

  deadline_ = new_deadline;
  SetImpl();
}

void QuicAlarm::CancelInternal(bool permanent) {
  if (IsSet()) {
    deadline_ = QuicTime::Zero();
    CancelImpl();
  }

  if (permanent) {
    delegate_.reset();
  }
}

bool QuicAlarm::IsPermanentlyCancelled() const { return delegate_ == nullptr; }

void QuicAlarm::Update(QuicTime new_deadline, QuicTime::Delta granularity) {
  if (IsPermanentlyCancelled()) {
    QUIC_BUG(quic_alarm_illegal_update)
        << "Update called after alarm is permanently cancelled. new_deadline:"
        << new_deadline << ", granularity:" << granularity;
    return;
  }

  if (!new_deadline.IsInitialized()) {
    Cancel();
    return;
  }
  if (std::abs((new_deadline - deadline_).ToMicroseconds()) <
      granularity.ToMicroseconds()) {
    return;
  }
  const bool was_set = IsSet();
  deadline_ = new_deadline;
  if (was_set) {
    UpdateImpl();
  } else {
    SetImpl();
  }
}

bool QuicAlarm::IsSet() const { return deadline_.IsInitialized(); }

void QuicAlarm::Fire() {
  if (!IsSet()) {
    return;
  }

  deadline_ = QuicTime::Zero();
  if (!IsPermanentlyCancelled()) {
    QuicConnectionContextSwitcher context_switcher(
        delegate_->GetConnectionContext());
    delegate_->OnAlarm();
  }
}

void QuicAlarm::UpdateImpl() {
  // CancelImpl and SetImpl take the new deadline by way of the deadline_
  // member, so save and restore deadline_ before canceling.
  const QuicTime new_deadline = deadline_;

  deadline_ = QuicTime::Zero();
  CancelImpl();

  deadline_ = new_deadline;
  SetImpl();
}

}  // namespace quic
