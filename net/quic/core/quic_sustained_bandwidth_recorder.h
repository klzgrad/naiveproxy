// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_SUSTAINED_BANDWIDTH_RECORDER_H_
#define NET_QUIC_CORE_QUIC_SUSTAINED_BANDWIDTH_RECORDER_H_

#include <cstdint>

#include "base/macros.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

namespace test {
class QuicSustainedBandwidthRecorderPeer;
}  // namespace test

// This class keeps track of a sustained bandwidth estimate to ultimately send
// to the client in a server config update message. A sustained bandwidth
// estimate is only marked as valid if the QuicSustainedBandwidthRecorder has
// been given uninterrupted reliable estimates over a certain period of time.
class QUIC_EXPORT_PRIVATE QuicSustainedBandwidthRecorder {
 public:
  QuicSustainedBandwidthRecorder();

  // As long as |in_recovery| is consistently false, multiple calls to this
  // method over a 3 * srtt period results in storage of a valid sustained
  // bandwidth estimate.
  // |time_now| is used as a max bandwidth timestamp if needed.
  void RecordEstimate(bool in_recovery,
                      bool in_slow_start,
                      QuicBandwidth bandwidth,
                      QuicTime estimate_time,
                      QuicWallTime wall_time,
                      QuicTime::Delta srtt);

  bool HasEstimate() const { return has_estimate_; }

  QuicBandwidth BandwidthEstimate() const {
    DCHECK(has_estimate_);
    return bandwidth_estimate_;
  }

  QuicBandwidth MaxBandwidthEstimate() const {
    DCHECK(has_estimate_);
    return max_bandwidth_estimate_;
  }

  int64_t MaxBandwidthTimestamp() const {
    DCHECK(has_estimate_);
    return max_bandwidth_timestamp_;
  }

  bool EstimateRecordedDuringSlowStart() const {
    DCHECK(has_estimate_);
    return bandwidth_estimate_recorded_during_slow_start_;
  }

 private:
  friend class test::QuicSustainedBandwidthRecorderPeer;

  // True if we have been able to calculate sustained bandwidth, over at least
  // one recording period (3 * rtt).
  bool has_estimate_;

  // True if the last call to RecordEstimate had a reliable estimate.
  bool is_recording_;

  // True if the current sustained bandwidth estimate was generated while in
  // slow start.
  bool bandwidth_estimate_recorded_during_slow_start_;

  // The latest sustained bandwidth estimate.
  QuicBandwidth bandwidth_estimate_;

  // The maximum sustained bandwidth seen over the lifetime of the connection.
  QuicBandwidth max_bandwidth_estimate_;

  // Timestamp indicating when the max_bandwidth_estimate_ was seen.
  int64_t max_bandwidth_timestamp_;

  // Timestamp marking the beginning of the latest recording period.
  QuicTime start_time_;

  DISALLOW_COPY_AND_ASSIGN(QuicSustainedBandwidthRecorder);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_SUSTAINED_BANDWIDTH_RECORDER_H_
