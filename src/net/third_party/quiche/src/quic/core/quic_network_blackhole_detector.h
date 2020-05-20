// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_NETWORK_BLACKHOLE_DETECTOR_H_
#define QUICHE_QUIC_CORE_QUIC_NETWORK_BLACKHOLE_DETECTOR_H_

#include "net/third_party/quiche/src/quic/core/quic_alarm.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_one_block_arena.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

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
class QUIC_EXPORT_PRIVATE QuicNetworkBlackholeDetector {
 public:
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

    // Called when the path degrading alarm fires.
    virtual void OnPathDegradingDetected() = 0;

    // Called when the path blackhole alarm fires.
    virtual void OnBlackholeDetected() = 0;
  };

  QuicNetworkBlackholeDetector(Delegate* delegate,
                               QuicConnectionArena* arena,
                               QuicAlarmFactory* alarm_factory);

  // Called to stop all detections.
  void StopDetection();

  // Called to restart path degrading or/and blackhole detections. Please note,
  // if both deadlines are set, |blackhole_deadline| must be later than
  // |path_degrading_deadline|.
  void RestartDetection(QuicTime path_degrading_deadline,
                        QuicTime blackhole_deadline);

  // Called when |alarm_| fires.
  void OnAlarm();

  // Returns true if |alarm_| is set.
  bool IsDetectionInProgress() const;

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicNetworkBlackholeDetectorPeer;

  Delegate* delegate_;  // Not owned.

  // Time that Delegate::OnPathDegrading will be called. 0 means no path
  // degrading detection is in progress.
  QuicTime path_degrading_deadline_;
  // Time that Delegate::OnBlackholeDetected will be called. 0 means no
  // blackhole detection is in progress.
  QuicTime blackhole_deadline_;

  QuicArenaScopedPtr<QuicAlarm> alarm_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_NETWORK_BLACKHOLE_DETECTOR_H_
