// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_bitrate_adjuster.h"

#include <algorithm>
#include <cstdint>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

using ::quic::QuicBandwidth;
using ::quic::QuicTime;
using ::quic::QuicTimeDelta;

// Whenever adjusting bitrate down, it is set to `kTargetBitrateMultiplier *
// bw`, where `bw` is typically windowed max bandwidth reported by BBR.  The
// current value selected is a bit arbitrary; ideally, we would adjust down to
// the application data goodput (i.e. goodput excluding all of the framing
// overhead), but that would either require us knowing how to compute the
// framing overhead correctly, or implementing our own application-level goodput
// monitoring.
constexpr float kTargetBitrateMultiplier = 0.9f;

// Avoid re-adjusting bitrate within N RTTs after adjusting it. Here, on a
// typical 20ms connection, 40 RTTs is 800ms.  Cap the limit at 3000ms.
constexpr float kMinTimeBetweenAdjustmentsInRtts = 40;
constexpr QuicTimeDelta kMaxTimeBetweenAdjustments =
    QuicTimeDelta::FromSeconds(3);

}  // namespace

void MoqtBitrateAdjuster::OnObjectAckReceived(
    uint64_t /*group_id*/, uint64_t /*object_id*/,
    QuicTimeDelta delta_from_deadline) {
  if (delta_from_deadline < QuicTimeDelta::Zero()) {
    // While adjusting down upon the first sign of packets getting late might
    // seem aggressive, note that:
    //   - By the time user occurs, this is already a user-visible issue (so, in
    //     some sense, this isn't aggressive enough).
    //   - The adjustment won't happen if we're already bellow `k * max_bw`, so
    //     if the delays are due to other factors like bufferbloat, the measured
    //     bandwidth will likely not result in a downwards adjustment.
    AttemptAdjustingDown();
  }
}

void MoqtBitrateAdjuster::AttemptAdjustingDown() {
  webtransport::SessionStats stats = session_->GetSessionStats();

  // Wait for a while after doing an adjustment.  There are non-trivial costs to
  // switching, so we should rate limit adjustments.
  QuicTimeDelta adjustment_delay =
      QuicTimeDelta(stats.smoothed_rtt * kMinTimeBetweenAdjustmentsInRtts);
  adjustment_delay = std::min(adjustment_delay, kMaxTimeBetweenAdjustments);
  QuicTime now = clock_->ApproximateNow();
  if (now - last_adjustment_time_ < adjustment_delay) {
    return;
  }

  // Only adjust downwards.
  QuicBandwidth target_bandwidth =
      kTargetBitrateMultiplier *
      QuicBandwidth::FromBitsPerSecond(stats.estimated_send_rate_bps);
  QuicBandwidth current_bandwidth = adjustable_->GetCurrentBitrate();
  if (current_bandwidth <= target_bandwidth) {
    return;
  }

  QUICHE_DLOG(INFO) << "Adjusting the bitrate from " << current_bandwidth
                    << " to " << target_bandwidth;
  bool success = adjustable_->AdjustBitrate(target_bandwidth);
  if (success) {
    last_adjustment_time_ = now;
  }
}

void MoqtBitrateAdjuster::OnObjectAckSupportKnown(bool supported) {
  QUICHE_DLOG_IF(WARNING, !supported)
      << "OBJECT_ACK not supported; bitrate adjustments will not work.";
}

}  // namespace moqt
