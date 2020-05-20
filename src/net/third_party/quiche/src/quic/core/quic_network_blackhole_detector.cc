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
      path_degrading_deadline_(QuicTime::Zero()),
      blackhole_deadline_(QuicTime::Zero()),
      alarm_(
          alarm_factory->CreateAlarm(arena->New<AlarmDelegate>(this), arena)) {}

void QuicNetworkBlackholeDetector::OnAlarm() {
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
}

void QuicNetworkBlackholeDetector::StopDetection() {
  alarm_->Cancel();
  path_degrading_deadline_ = QuicTime::Zero();
  blackhole_deadline_ = QuicTime::Zero();
}

void QuicNetworkBlackholeDetector::RestartDetection(
    QuicTime path_degrading_deadline,
    QuicTime blackhole_deadline) {
  path_degrading_deadline_ = path_degrading_deadline;
  blackhole_deadline_ = blackhole_deadline;
  QUIC_BUG_IF(path_degrading_deadline_.IsInitialized() &&
              blackhole_deadline_.IsInitialized() &&
              path_degrading_deadline_ > blackhole_deadline_)
      << "Path degrading timeout is later than blackhole detection timeout";
  alarm_->Update(path_degrading_deadline_, kAlarmGranularity);
  if (alarm_->IsSet()) {
    return;
  }
  alarm_->Update(blackhole_deadline_, kAlarmGranularity);
}

bool QuicNetworkBlackholeDetector::IsDetectionInProgress() const {
  return alarm_->IsSet();
}

}  // namespace quic
