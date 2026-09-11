// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_NETWORK_BLACKHOLE_DETECTOR_H_
#define QUICHE_QUIC_CORE_QUIC_NETWORK_BLACKHOLE_DETECTOR_H_

#include "quiche/quic/core/quic_connection_alarms.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

namespace test {
class QuicConnectionPeer;
class QuicNetworkBlackholeDetectorPeer;
}  // namespace test

// QuicNetworkBlackholeDetector can detect path degrading and/or network
// blackhole. If both detections are in progress, detector will be in path
// degrading detection mode. After reporting path degrading detected, detector
// switches to blackhole detection mode. So blackhole detection deadline must
// be later than path degrading deadline.
class QUICHE_EXPORT QuicNetworkBlackholeDetector {
 public:
  class QUICHE_EXPORT Delegate {
   public:
    virtual ~Delegate() {}

    // Called when the path degrading alarm fires.
    virtual void OnPathDegradingDetected() = 0;

    // Called when the path blackhole alarm fires.
    virtual void OnBlackholeDetected() = 0;

    // Called when the path mtu reduction alarm fires.
    virtual void OnPathMtuReductionDetected() = 0;
  };

  QuicNetworkBlackholeDetector(Delegate* delegate, QuicAlarmProxy alarm);

  // Called to stop all detections. If |permanent|, the alarm will be cancelled
  // permanently and future calls to RestartDetection will be no-op.
  void StopDetection(bool permanent);

  // Called to restart path degrading, path mtu reduction and blackhole
  // detections. Please note, if |blackhole_deadline| is set, it must be the
  // furthest in the future of all deadlines.
  void RestartDetection(QuicTime path_degrading_deadline,
                        QuicTime blackhole_deadline,
                        QuicTime path_mtu_reduction_deadline);

  // Called when |alarm_| fires.
  void OnAlarm();

  // Returns true if |alarm_| is set.
  bool IsDetectionInProgress() const;

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicNetworkBlackholeDetectorPeer;

  QuicTime GetEarliestDeadline() const;
  QuicTime GetLastDeadline() const;

  // Update alarm to the next deadline.
  void UpdateAlarm();

  Delegate* delegate_;  // Not owned.

  // Time that Delegate::OnPathDegrading will be called. 0 means no path
  // degrading detection is in progress.
  QuicTime path_degrading_deadline_ = QuicTime::Zero();
  // Time that Delegate::OnBlackholeDetected will be called. 0 means no
  // blackhole detection is in progress.
  QuicTime blackhole_deadline_ = QuicTime::Zero();
  // Time that Delegate::OnPathMtuReductionDetected will be called. 0 means no
  // path mtu reduction detection is in progress.
  QuicTime path_mtu_reduction_deadline_ = QuicTime::Zero();

  QuicAlarmProxy alarm_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_NETWORK_BLACKHOLE_DETECTOR_H_
