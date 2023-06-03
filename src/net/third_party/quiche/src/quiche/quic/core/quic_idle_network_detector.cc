// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_idle_network_detector.h"

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {

class AlarmDelegate : public QuicAlarm::DelegateWithContext {
 public:
  explicit AlarmDelegate(QuicIdleNetworkDetector* detector,
                         QuicConnectionContext* context)
      : QuicAlarm::DelegateWithContext(context), detector_(detector) {}
  AlarmDelegate(const AlarmDelegate&) = delete;
  AlarmDelegate& operator=(const AlarmDelegate&) = delete;

  void OnAlarm() override { detector_->OnAlarm(); }

 private:
  QuicIdleNetworkDetector* detector_;
};

}  // namespace

QuicIdleNetworkDetector::QuicIdleNetworkDetector(
    Delegate* delegate, QuicTime now, QuicConnectionArena* arena,
    QuicAlarmFactory* alarm_factory, QuicConnectionContext* context)
    : delegate_(delegate),
      start_time_(now),
      handshake_timeout_(QuicTime::Delta::Infinite()),
      time_of_last_received_packet_(now),
      time_of_first_packet_sent_after_receiving_(QuicTime::Zero()),
      idle_network_timeout_(QuicTime::Delta::Infinite()),
      bandwidth_update_timeout_(QuicTime::Delta::Infinite()),
      alarm_(alarm_factory->CreateAlarm(
          arena->New<AlarmDelegate>(this, context), arena)) {}

void QuicIdleNetworkDetector::OnAlarm() {
  if (!bandwidth_update_timeout_.IsInfinite()) {
    QUICHE_DCHECK(handshake_timeout_.IsInfinite());
    bandwidth_update_timeout_ = QuicTime::Delta::Infinite();
    SetAlarm();
    delegate_->OnBandwidthUpdateTimeout();
    return;
  }
  if (handshake_timeout_.IsInfinite()) {
    delegate_->OnIdleNetworkDetected();
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
  bandwidth_update_timeout_ = QuicTime::Delta::Infinite();

  if (GetQuicRestartFlag(
          quic_enable_sending_bandwidth_estimate_when_network_idle_v2) &&
      handshake_timeout_.IsInfinite()) {
    QUIC_RESTART_FLAG_COUNT_N(
        quic_enable_sending_bandwidth_estimate_when_network_idle_v2, 1, 3);
    bandwidth_update_timeout_ = idle_network_timeout_ * 0.5;
  }

  SetAlarm();
}

void QuicIdleNetworkDetector::StopDetection() {
  alarm_->PermanentCancel();
  handshake_timeout_ = QuicTime::Delta::Infinite();
  idle_network_timeout_ = QuicTime::Delta::Infinite();
  handshake_timeout_ = QuicTime::Delta::Infinite();
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
  QuicTime new_deadline = QuicTime::Zero();
  if (!handshake_timeout_.IsInfinite()) {
    new_deadline = start_time_ + handshake_timeout_;
  }
  if (!idle_network_timeout_.IsInfinite()) {
    const QuicTime idle_network_deadline = GetIdleNetworkDeadline();
    if (new_deadline.IsInitialized()) {
      new_deadline = std::min(new_deadline, idle_network_deadline);
    } else {
      new_deadline = idle_network_deadline;
    }
  }
  if (!bandwidth_update_timeout_.IsInfinite()) {
    new_deadline = std::min(new_deadline, GetBandwidthUpdateDeadline());
  }
  alarm_->Update(new_deadline, kAlarmGranularity);
}

void QuicIdleNetworkDetector::MaybeSetAlarmOnSentPacket(
    QuicTime::Delta pto_delay) {
  QUICHE_DCHECK(shorter_idle_timeout_on_sent_packet_);
  if (!handshake_timeout_.IsInfinite() || !alarm_->IsSet()) {
    SetAlarm();
    return;
  }
  // Make sure connection will be alive for another PTO.
  const QuicTime deadline = alarm_->deadline();
  const QuicTime min_deadline = last_network_activity_time() + pto_delay;
  if (deadline > min_deadline) {
    return;
  }
  alarm_->Update(min_deadline, kAlarmGranularity);
}

QuicTime QuicIdleNetworkDetector::GetIdleNetworkDeadline() const {
  if (idle_network_timeout_.IsInfinite()) {
    return QuicTime::Zero();
  }
  return last_network_activity_time() + idle_network_timeout_;
}

QuicTime QuicIdleNetworkDetector::GetBandwidthUpdateDeadline() const {
  QUICHE_DCHECK(!bandwidth_update_timeout_.IsInfinite());
  return last_network_activity_time() + bandwidth_update_timeout_;
}

}  // namespace quic
