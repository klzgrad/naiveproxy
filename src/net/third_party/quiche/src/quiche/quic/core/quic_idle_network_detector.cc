// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_idle_network_detector.h"

#include <algorithm>

#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

QuicIdleNetworkDetector::QuicIdleNetworkDetector(Delegate* delegate,
                                                 QuicTime now,
                                                 QuicAlarmProxy alarm)
    : delegate_(delegate),
      start_time_(now),
      handshake_timeout_(QuicTime::Delta::Infinite()),
      time_of_last_received_packet_(now),
      time_of_first_packet_sent_after_receiving_(QuicTime::Zero()),
      idle_network_timeout_(QuicTime::Delta::Infinite()),
      alarm_(alarm) {}

void QuicIdleNetworkDetector::OnAlarm() {
  if (handshake_timeout_.IsInfinite()) {
    if (last_alarm_type_ != AlarmType::kMemoryReductionTimeout) {
      delegate_->OnIdleNetworkDetected();
    } else {
      QUICHE_DCHECK(last_alarm_type_ == AlarmType::kMemoryReductionTimeout);
      delegate_->OnMemoryReductionTimeout();
      // Rearms the alarm to idle network deadline.
      UpdateAlarm(AlarmType::kIdleNetworkTimeout, GetIdleNetworkDeadline());
    }
    return;
  }
  if (idle_network_timeout_.IsInfinite()) {
    delegate_->OnHandshakeTimeout();
    return;
  }
  if (last_network_activity_time() + idle_network_timeout_ >
      start_time_ + handshake_timeout_) {
    delegate_->OnHandshakeTimeout();
    return;
  }
  delegate_->OnIdleNetworkDetected();
}

void QuicIdleNetworkDetector::SetTimeouts(
    QuicTime::Delta handshake_timeout, QuicTime::Delta idle_network_timeout) {
  handshake_timeout_ = handshake_timeout;
  idle_network_timeout_ = idle_network_timeout;

  SetAlarm();
}

void QuicIdleNetworkDetector::StopDetection() {
  alarm_.PermanentCancel();
  handshake_timeout_ = QuicTime::Delta::Infinite();
  idle_network_timeout_ = QuicTime::Delta::Infinite();
  memory_reduction_timeout_ = QuicTime::Delta::Infinite();
  last_alarm_type_ = AlarmType::kUnknown;
  stopped_ = true;
}

void QuicIdleNetworkDetector::OnPacketSent(QuicTime now,
                                           QuicTime::Delta pto_delay) {
  if (time_of_first_packet_sent_after_receiving_ >
      time_of_last_received_packet_) {
    return;
  }
  time_of_first_packet_sent_after_receiving_ =
      std::max(time_of_first_packet_sent_after_receiving_, now);
  if (shorter_idle_timeout_on_sent_packet_) {
    MaybeSetAlarmOnSentPacket(pto_delay);
    return;
  }

  SetAlarm();
}

void QuicIdleNetworkDetector::OnPacketReceived(QuicTime now) {
  time_of_last_received_packet_ = std::max(time_of_last_received_packet_, now);

  SetAlarm();
}

bool QuicIdleNetworkDetector::ShouldMemoryReductionTimeoutBeUsed() const {
  if (shorter_idle_timeout_on_sent_packet_) {
    // No benefit to consider memory reduction if shorter idle timeout on sent
    // packet is enabled.
    return false;
  }
  if (!handshake_timeout_.IsInfinite()) {
    // No benefit to consider memory reduction if handshake has not completed.
    return false;
  }
  if (memory_reduction_timeout_ >= idle_network_timeout_ ||
      (idle_network_timeout_ - memory_reduction_timeout_ <
       QuicTime::Delta::FromSeconds(60))) {
    // No benefit to consider memory reduction if memory reduction timeout is
    // too close to idle network timeout.
    return false;
  }
  return true;
}

void QuicIdleNetworkDetector::SetAlarm() {
  if (stopped_) {
    // TODO(wub): If this QUIC_BUG fires, it indicates a problem in the
    // QuicConnection, which somehow called this function while disconnected.
    // That problem needs to be fixed.
    QUIC_BUG(quic_idle_detector_set_alarm_after_stopped)
        << "SetAlarm called after stopped";
    return;
  }
  // Set alarm to the nearer deadline.
  AlarmType alarm_type = AlarmType::kUnknown;
  QuicTime new_deadline = QuicTime::Zero();
  if (!handshake_timeout_.IsInfinite()) {
    alarm_type = AlarmType::kHandshakeTimeout;
    new_deadline = start_time_ + handshake_timeout_;
  }
  if (!idle_network_timeout_.IsInfinite()) {
    const QuicTime idle_network_deadline = GetIdleNetworkDeadline();
    if (new_deadline.IsInitialized()) {
      new_deadline = std::min(new_deadline, idle_network_deadline);
    } else {
      new_deadline = idle_network_deadline;
    }
    if (new_deadline == idle_network_deadline) {
      alarm_type = AlarmType::kIdleNetworkTimeout;
    }
  }
  if (!memory_reduction_timeout_.IsInfinite() &&
      ShouldMemoryReductionTimeoutBeUsed()) {
    alarm_type = AlarmType::kMemoryReductionTimeout;
    new_deadline = last_network_activity_time() + memory_reduction_timeout_;
  }
  UpdateAlarm(alarm_type, new_deadline);
}

void QuicIdleNetworkDetector::MaybeSetAlarmOnSentPacket(
    QuicTime::Delta pto_delay) {
  QUICHE_DCHECK(shorter_idle_timeout_on_sent_packet_);
  if (!handshake_timeout_.IsInfinite() || !alarm_.IsSet()) {
    SetAlarm();
    return;
  }
  // Make sure connection will be alive for another PTO.
  const QuicTime deadline = alarm_.deadline();
  const QuicTime min_deadline = last_network_activity_time() + pto_delay;
  if (deadline > min_deadline) {
    return;
  }
  UpdateAlarm(AlarmType::kPtoDelay, min_deadline);
}

QuicTime QuicIdleNetworkDetector::GetIdleNetworkDeadline() const {
  if (idle_network_timeout_.IsInfinite()) {
    return QuicTime::Zero();
  }
  return last_network_activity_time() + idle_network_timeout_;
}

}  // namespace quic
