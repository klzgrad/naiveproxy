// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_network_blackhole_detector.h"

#include <algorithm>

#include "quiche/quic/core/quic_constants.h"

namespace quic {

QuicNetworkBlackholeDetector::QuicNetworkBlackholeDetector(Delegate* delegate,
                                                           QuicAlarm* alarm)
    : delegate_(delegate), alarm_(*alarm) {}

void QuicNetworkBlackholeDetector::OnAlarm() {
  QuicTime next_deadline = GetEarliestDeadline();
  if (!next_deadline.IsInitialized()) {
    QUIC_BUG(quic_bug_10328_1) << "BlackholeDetector alarm fired unexpectedly";
    return;
  }

  QUIC_DVLOG(1) << "BlackholeDetector alarm firing. next_deadline:"
                << next_deadline
                << ", path_degrading_deadline_:" << path_degrading_deadline_
                << ", path_mtu_reduction_deadline_:"
                << path_mtu_reduction_deadline_
                << ", blackhole_deadline_:" << blackhole_deadline_;
  if (path_degrading_deadline_ == next_deadline) {
    path_degrading_deadline_ = QuicTime::Zero();
    delegate_->OnPathDegradingDetected();
  }

  if (path_mtu_reduction_deadline_ == next_deadline) {
    path_mtu_reduction_deadline_ = QuicTime::Zero();
    delegate_->OnPathMtuReductionDetected();
  }

  if (blackhole_deadline_ == next_deadline) {
    blackhole_deadline_ = QuicTime::Zero();
    delegate_->OnBlackholeDetected();
  }

  UpdateAlarm();
}

void QuicNetworkBlackholeDetector::StopDetection(bool permanent) {
  if (permanent) {
    alarm_.PermanentCancel();
  } else {
    alarm_.Cancel();
  }
  path_degrading_deadline_ = QuicTime::Zero();
  blackhole_deadline_ = QuicTime::Zero();
  path_mtu_reduction_deadline_ = QuicTime::Zero();
}

void QuicNetworkBlackholeDetector::RestartDetection(
    QuicTime path_degrading_deadline, QuicTime blackhole_deadline,
    QuicTime path_mtu_reduction_deadline) {
  path_degrading_deadline_ = path_degrading_deadline;
  blackhole_deadline_ = blackhole_deadline;
  path_mtu_reduction_deadline_ = path_mtu_reduction_deadline;

  QUIC_BUG_IF(quic_bug_12708_1, blackhole_deadline_.IsInitialized() &&
                                    blackhole_deadline_ != GetLastDeadline())
      << "Blackhole detection deadline should be the last deadline.";

  UpdateAlarm();
}

QuicTime QuicNetworkBlackholeDetector::GetEarliestDeadline() const {
  QuicTime result = QuicTime::Zero();
  for (QuicTime t : {path_degrading_deadline_, blackhole_deadline_,
                     path_mtu_reduction_deadline_}) {
    if (!t.IsInitialized()) {
      continue;
    }

    if (!result.IsInitialized() || t < result) {
      result = t;
    }
  }

  return result;
}

QuicTime QuicNetworkBlackholeDetector::GetLastDeadline() const {
  return std::max({path_degrading_deadline_, blackhole_deadline_,
                   path_mtu_reduction_deadline_});
}

void QuicNetworkBlackholeDetector::UpdateAlarm() const {
  // If called after OnBlackholeDetected(), the alarm may have been permanently
  // cancelled and is not safe to be armed again.
  if (alarm_.IsPermanentlyCancelled()) {
    return;
  }

  QuicTime next_deadline = GetEarliestDeadline();

  QUIC_DVLOG(1) << "Updating alarm. next_deadline:" << next_deadline
                << ", path_degrading_deadline_:" << path_degrading_deadline_
                << ", path_mtu_reduction_deadline_:"
                << path_mtu_reduction_deadline_
                << ", blackhole_deadline_:" << blackhole_deadline_;

  alarm_.Update(next_deadline, kAlarmGranularity);
}

bool QuicNetworkBlackholeDetector::IsDetectionInProgress() const {
  return alarm_.IsSet();
}

}  // namespace quic
