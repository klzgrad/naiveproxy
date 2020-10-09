// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_network_blackhole_detector.h"

#include "net/third_party/quiche/src/quic/core/quic_constants.h"

namespace quic {

namespace {

class AlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit AlarmDelegate(QuicNetworkBlackholeDetector* detector)
      : detector_(detector) {}
  AlarmDelegate(const AlarmDelegate&) = delete;
  AlarmDelegate& operator=(const AlarmDelegate&) = delete;

  void OnAlarm() override { detector_->OnAlarm(); }

 private:
  QuicNetworkBlackholeDetector* detector_;
};

}  // namespace

QuicNetworkBlackholeDetector::QuicNetworkBlackholeDetector(
    Delegate* delegate,
    QuicConnectionArena* arena,
    QuicAlarmFactory* alarm_factory)
    : delegate_(delegate),
      alarm_(
          alarm_factory->CreateAlarm(arena->New<AlarmDelegate>(this), arena)) {}

void QuicNetworkBlackholeDetector::OnAlarm() {
  if (!revert_mtu_after_two_ptos_) {
    if (path_degrading_deadline_.IsInitialized()) {
      path_degrading_deadline_ = QuicTime::Zero();
      delegate_->OnPathDegradingDetected();
      // Switch to blackhole detection mode.
      alarm_->Update(blackhole_deadline_, kAlarmGranularity);
      return;
    }
    if (blackhole_deadline_.IsInitialized()) {
      blackhole_deadline_ = QuicTime::Zero();
      delegate_->OnBlackholeDetected();
    }
    return;
  }

  QuicTime next_deadline = GetEarliestDeadline();
  if (!next_deadline.IsInitialized()) {
    QUIC_BUG << "BlackholeDetector alarm fired unexpectedly";
    return;
  }

  QUIC_DLOG(INFO) << "BlackholeDetector alarm firing. next_deadline:"
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

void QuicNetworkBlackholeDetector::StopDetection() {
  alarm_->Cancel();
  path_degrading_deadline_ = QuicTime::Zero();
  blackhole_deadline_ = QuicTime::Zero();
  path_mtu_reduction_deadline_ = QuicTime::Zero();
}

void QuicNetworkBlackholeDetector::RestartDetection(
    QuicTime path_degrading_deadline,
    QuicTime blackhole_deadline,
    QuicTime path_mtu_reduction_deadline) {
  path_degrading_deadline_ = path_degrading_deadline;
  blackhole_deadline_ = blackhole_deadline;
  path_mtu_reduction_deadline_ = path_mtu_reduction_deadline;

  if (!revert_mtu_after_two_ptos_) {
    QUIC_BUG_IF(path_degrading_deadline_.IsInitialized() &&
                blackhole_deadline_.IsInitialized() &&
                path_degrading_deadline_ > blackhole_deadline_)
        << "Path degrading timeout is later than blackhole detection timeout";
  } else {
    QUIC_BUG_IF(blackhole_deadline_.IsInitialized() &&
                blackhole_deadline_ != GetLastDeadline())
        << "Blackhole detection deadline should be the last deadline.";
  }

  if (!revert_mtu_after_two_ptos_) {
    alarm_->Update(path_degrading_deadline_, kAlarmGranularity);
    if (alarm_->IsSet()) {
      return;
    }
    alarm_->Update(blackhole_deadline_, kAlarmGranularity);
  } else {
    UpdateAlarm();
  }
}

QuicTime QuicNetworkBlackholeDetector::GetEarliestDeadline() const {
  DCHECK(revert_mtu_after_two_ptos_);

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
  DCHECK(revert_mtu_after_two_ptos_);
  return std::max({path_degrading_deadline_, blackhole_deadline_,
                   path_mtu_reduction_deadline_});
}

void QuicNetworkBlackholeDetector::UpdateAlarm() const {
  DCHECK(revert_mtu_after_two_ptos_);

  QuicTime next_deadline = GetEarliestDeadline();

  QUIC_DLOG(INFO) << "Updating alarm. next_deadline:" << next_deadline
                  << ", path_degrading_deadline_:" << path_degrading_deadline_
                  << ", path_mtu_reduction_deadline_:"
                  << path_mtu_reduction_deadline_
                  << ", blackhole_deadline_:" << blackhole_deadline_;

  alarm_->Update(next_deadline, kAlarmGranularity);
}

bool QuicNetworkBlackholeDetector::IsDetectionInProgress() const {
  return alarm_->IsSet();
}

}  // namespace quic
