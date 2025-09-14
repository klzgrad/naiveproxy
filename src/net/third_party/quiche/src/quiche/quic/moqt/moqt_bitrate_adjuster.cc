// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_bitrate_adjuster.h"

#include <cstdint>
#include <cstdlib>
#include <optional>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

using ::quic::QuicBandwidth;
using ::quic::QuicTime;
using ::quic::QuicTimeDelta;

}  // namespace

void MoqtBitrateAdjuster::Start() {
  if (start_time_.IsInitialized()) {
    QUICHE_BUG(MoqtBitrateAdjuster_double_init)
        << "MoqtBitrateAdjuster::Start() called more than once.";
    return;
  }
  start_time_ = clock_->Now();
}

void MoqtBitrateAdjuster::OnObjectAckReceived(
    uint64_t /*group_id*/, uint64_t /*object_id*/,
    QuicTimeDelta delta_from_deadline) {
  if (!start_time_.IsInitialized()) {
    return;
  }

  const QuicTime earliest_action_time = start_time_ + parameters_.initial_delay;
  if (clock_->Now() < earliest_action_time) {
    return;
  }

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
  QuicBandwidth target_bandwidth =
      parameters_.target_bitrate_multiplier_down *
      QuicBandwidth::FromBitsPerSecond(stats.estimated_send_rate_bps);
  QUICHE_DLOG(INFO) << "Adjusting the bitrate down to " << target_bandwidth;
  adjustable_->ConsiderAdjustingBitrate(target_bandwidth,
                                        BitrateAdjustmentType::kDown);
}

void MoqtBitrateAdjuster::OnObjectAckSupportKnown(
    std::optional<quic::QuicTimeDelta> time_window) {
  if (!time_window.has_value() || *time_window <= QuicTimeDelta::Zero()) {
    QUICHE_DLOG(WARNING)
        << "OBJECT_ACK not supported; bitrate adjustments will not work.";
    return;
  }
  time_window_ = *time_window;
  Start();
}

bool ShouldIgnoreBitrateAdjustment(quic::QuicBandwidth new_bitrate,
                                   BitrateAdjustmentType type,
                                   quic::QuicBandwidth old_bitrate,
                                   float min_change) {
  const float min_change_bps = old_bitrate.ToBitsPerSecond() * min_change;
  const float change_bps =
      new_bitrate.ToBitsPerSecond() - old_bitrate.ToBitsPerSecond();
  if (std::abs(change_bps) < min_change_bps) {
    return true;
  }

  switch (type) {
    case moqt::BitrateAdjustmentType::kDown:
      if (new_bitrate >= old_bitrate) {
        return true;
      }
      break;
    case moqt::BitrateAdjustmentType::kUp:
      if (old_bitrate >= new_bitrate) {
        return true;
      }
      break;
  }
  return false;
}

}  // namespace moqt
