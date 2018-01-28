// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_sustained_bandwidth_recorder.h"

#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

QuicSustainedBandwidthRecorder::QuicSustainedBandwidthRecorder()
    : has_estimate_(false),
      is_recording_(false),
      bandwidth_estimate_recorded_during_slow_start_(false),
      bandwidth_estimate_(QuicBandwidth::Zero()),
      max_bandwidth_estimate_(QuicBandwidth::Zero()),
      max_bandwidth_timestamp_(0),
      start_time_(QuicTime::Zero()) {}

void QuicSustainedBandwidthRecorder::RecordEstimate(bool in_recovery,
                                                    bool in_slow_start,
                                                    QuicBandwidth bandwidth,
                                                    QuicTime estimate_time,
                                                    QuicWallTime wall_time,
                                                    QuicTime::Delta srtt) {
  if (in_recovery) {
    is_recording_ = false;
    QUIC_DVLOG(1) << "Stopped recording at: "
                  << estimate_time.ToDebuggingValue();
    return;
  }

  if (!is_recording_) {
    // This is the first estimate of a new recording period.
    start_time_ = estimate_time;
    is_recording_ = true;
    QUIC_DVLOG(1) << "Started recording at: " << start_time_.ToDebuggingValue();
    return;
  }

  // If we have been recording for at least 3 * srtt, then record the latest
  // bandwidth estimate as a valid sustained bandwidth estimate.
  if (estimate_time - start_time_ >= 3 * srtt) {
    has_estimate_ = true;
    bandwidth_estimate_recorded_during_slow_start_ = in_slow_start;
    bandwidth_estimate_ = bandwidth;
    QUIC_DVLOG(1) << "New sustained bandwidth estimate (KBytes/s): "
                  << bandwidth_estimate_.ToKBytesPerSecond();
  }

  // Check for an increase in max bandwidth.
  if (bandwidth > max_bandwidth_estimate_) {
    max_bandwidth_estimate_ = bandwidth;
    max_bandwidth_timestamp_ = wall_time.ToUNIXSeconds();
    QUIC_DVLOG(1) << "New max bandwidth estimate (KBytes/s): "
                  << max_bandwidth_estimate_.ToKBytesPerSecond();
  }
}

}  // namespace net
