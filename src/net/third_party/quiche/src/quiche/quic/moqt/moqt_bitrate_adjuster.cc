// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_bitrate_adjuster.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/time/time.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_probe_manager.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

using ::quic::QuicBandwidth;
using ::quic::QuicByteCount;
using ::quic::QuicTime;
using ::quic::QuicTimeDelta;

}  // namespace

MoqtBitrateAdjuster::MoqtBitrateAdjuster(
    const quic::QuicClock* clock, webtransport::Session* session,
    std::unique_ptr<MoqtProbeManagerInterface> probe_manager,
    BitrateAdjustable* adjustable)
    : clock_(clock),
      session_(session),
      adjustable_(adjustable),
      probe_manager_(std::move(probe_manager)) {}

MoqtBitrateAdjuster::MoqtBitrateAdjuster(const quic::QuicClock* clock,
                                         webtransport::Session* session,
                                         quic::QuicAlarmFactory* alarm_factory,
                                         BitrateAdjustable* adjustable)
    : MoqtBitrateAdjuster(clock, session,
                          std::make_unique<MoqtProbeManager>(
                              session, clock, *alarm_factory, &trace_recorder_),
                          adjustable) {}

void MoqtBitrateAdjuster::Start() {
  if (start_time_.IsInitialized()) {
    QUICHE_BUG(MoqtBitrateAdjuster_double_init)
        << "MoqtBitrateAdjuster::Start() called more than once.";
    return;
  }

  start_time_ = clock_->Now();
  outstanding_objects_.emplace(
      /*max_out_of_order_objects=*/parameters_
          .quality_level_reordering_thresholds[0]);
}

void MoqtBitrateAdjuster::OnObjectAckReceived(
    Location location, QuicTimeDelta delta_from_deadline) {
  if (!start_time_.IsInitialized() || !outstanding_objects_.has_value()) {
    return;
  }
  const QuicTime now = clock_->Now();

  // Update the state.
  int reordering_delta = outstanding_objects_->OnObjectAcked(location);

  // Decide whether to act based on the latest signal.
  if (!ShouldUseAckAsActionSignal(location)) {
    return;
  }
  const ConnectionQualityLevel quality_level =
      GetQualityLevel(reordering_delta, delta_from_deadline);

  if (quality_level == ConnectionQualityLevel::kStable) {
    if (!in_stable_state_since_.has_value()) {
      in_stable_state_since_ = now;
    }
  } else {
    in_stable_state_since_.reset();
  }

  if (quality_level <= ConnectionQualityLevel::kVeryPrecarious) {
    probe_manager_->StopProbe();
  }

  if (quality_level <= ConnectionQualityLevel::kCritical) {
    AttemptAdjustingDown();
  }
  MaybeStartProbing(now);
}

bool MoqtBitrateAdjuster::ShouldUseAckAsActionSignal(Location location) {
  // Allow for some time to pass for the connection to reach the point at which
  // the rate adaptation signals can become useful.
  const QuicTime earliest_action_time = start_time_ + parameters_.initial_delay;
  const bool too_early_in_the_connection = clock_->Now() < earliest_action_time;

  // Ignore out-of-order acks for the purpose of deciding whether to adjust up
  // or down.  Generally, if an ack is out of order, the bitrate adjuster has
  // already reacted to the later object appropriately.
  const bool is_out_of_order_ack = location < last_acked_object_;
  last_acked_object_ = location;

  return !too_early_in_the_connection && !is_out_of_order_ack;
}

ConnectionQualityLevel MoqtBitrateAdjuster::GetQualityLevel(
    int reordering_delta, quic::QuicTimeDelta delta_from_deadline) const {
  for (int i = 0; i < kNumQualityLevels - 1; ++i) {
    const ConnectionQualityLevel level = static_cast<ConnectionQualityLevel>(i);

    const int max_out_of_order_objects =
        parameters_.quality_level_reordering_thresholds[i];
    const bool has_exceeded_max_out_of_order =
        reordering_delta > max_out_of_order_objects;
    if (has_exceeded_max_out_of_order) {
      QUICHE_DLOG(INFO) << "Downgrading connection quality down to " << level
                        << " due to reordering, delta: " << reordering_delta;
      return level;
    }

    const float time_delta_threshold =
        parameters_.quality_level_time_thresholds[i];
    const bool time_delta_too_close =
        delta_from_deadline < time_delta_threshold * time_window_;
    if (time_delta_too_close) {
      QUICHE_DLOG(INFO) << "Downgrading connection quality down to " << level
                        << "due to object arriving too late, time delta: "
                        << delta_from_deadline;
      return level;
    }
  }

  return ConnectionQualityLevel::kStable;
}

void MoqtBitrateAdjuster::AttemptAdjustingDown() {
  webtransport::SessionStats stats = session_->GetSessionStats();
  QuicBandwidth target_bandwidth =
      parameters_.target_bitrate_multiplier_down *
      QuicBandwidth::FromBitsPerSecond(stats.estimated_send_rate_bps);
  QUICHE_DLOG(INFO) << "Adjusting the bitrate down to " << target_bandwidth;
  SuggestNewBitrate(target_bandwidth, BitrateAdjustmentType::kDown);
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

void MoqtBitrateAdjuster::SuggestNewBitrate(quic::QuicBandwidth bitrate,
                                            BitrateAdjustmentType type) {
  adjustable_->ConsiderAdjustingBitrate(bitrate, type);
  trace_recorder_.RecordTargetBitrateSet(bitrate);
}

void MoqtBitrateAdjuster::OnNewObjectEnqueued(Location location) {
  if (!start_time_.IsInitialized() || !outstanding_objects_.has_value()) {
    return;
  }
  outstanding_objects_->OnObjectAdded(location);
}

void MoqtBitrateAdjuster::MaybeStartProbing(QuicTime now) {
  if (!in_stable_state_since_.has_value() || probe_manager_->HasActiveProbe() ||
      !adjustable_->CouldUseExtraBandwidth()) {
    return;
  }

  const QuicTime start_probe_after =
      *in_stable_state_since_ + parameters_.time_before_probing;
  if (now < start_probe_after) {
    return;
  }

  const webtransport::SessionStats stats = session_->GetSessionStats();
  const QuicBandwidth current_bandwidth =
      std::min(adjustable_->GetCurrentBitrate(),
               QuicBandwidth::FromBitsPerSecond(stats.estimated_send_rate_bps));
  const QuicBandwidth target_bandwidth =
      parameters_.probe_increase_target * current_bandwidth;
  // Approximation of PTO as defined in RFC 9002, Section 6.2.1.
  const QuicTimeDelta pto(stats.smoothed_rtt + 4 * stats.rtt_variation);
  const QuicByteCount probe_size =
      parameters_.round_trips_for_probe * pto * target_bandwidth;
  const QuicTimeDelta probe_timeout =
      current_bandwidth.TransferTime(probe_size) + pto;
  std::optional<ProbeId> probe_id = probe_manager_->StartProbe(
      probe_size, probe_timeout,
      absl::bind_front(&MoqtBitrateAdjuster::OnProbeResult, this, stats));

  if (!probe_id.has_value()) {
    QUICHE_DLOG(WARNING)
        << "Failed to create a new probe. Most likely blocked by flow control.";
    in_stable_state_since_.reset();
  }
}

void MoqtBitrateAdjuster::OnProbeResult(
    const webtransport::SessionStats& old_stats, const ProbeResult& result) {
  // Clear the timer before the next probe regardless of what we do with the
  // results of this one.
  in_stable_state_since_.reset();

  // It is possible for the probe to be cancelled due to poor connection status
  // (among other reasons).  In that case, we shouldn't attempt to increase the
  // bitrate.
  if (result.status == ProbeStatus::kAborted) {
    return;
  }

  // While the probe can also be timed out, it is still possible to get a useful
  // bandwidth increase from a timed-out probe.  Check for a probe duration
  // threshold instead.
  const webtransport::SessionStats stats = session_->GetSessionStats();
  const double probe_duration_in_rtts =
      result.time_elapsed.ToMicroseconds() /
      absl::ToDoubleMicroseconds(stats.smoothed_rtt);
  if (probe_duration_in_rtts < parameters_.min_probe_duration_in_rtts) {
    return;
  }

  // Use the bandwidth estimate from the congestion controller as the main
  // input.
  const QuicBandwidth congestion_control_bandwidth =
      QuicBandwidth::FromBitsPerSecond(stats.estimated_send_rate_bps);

  // Use the long-term average over the duration of entire probe as the input.
  const QuicByteCount acked_bytes = stats.application_bytes_acknowledged -
                                    old_stats.application_bytes_acknowledged;
  const QuicBandwidth average_goodput_over_probe_duration =
      QuicBandwidth::FromBytesAndTimeDelta(acked_bytes, result.time_elapsed);
  // If the probe has lasted N RTTs, assume that it could have accidentally
  // included an extra RTT worth of data due to ack aggregation.
  const double average_goodput_adjustment =
      probe_duration_in_rtts / (probe_duration_in_rtts + 1);

  // Use congestion controller bandwidth with the long-term average as a limit.
  const QuicBandwidth target_bandwidth = std::min(
      congestion_control_bandwidth * parameters_.target_bitrate_multiplier_up,
      average_goodput_adjustment * average_goodput_over_probe_duration);

  QUICHE_DLOG(INFO) << "Adjusting the bitrate up to " << target_bandwidth;
  SuggestNewBitrate(target_bandwidth, BitrateAdjustmentType::kUp);
}

std::string ConnectionQualityLevelToString(ConnectionQualityLevel level) {
  switch (level) {
    case ConnectionQualityLevel::kCritical:
      return "Critical";
    case ConnectionQualityLevel::kVeryPrecarious:
      return "VeryPrecarious";
    case ConnectionQualityLevel::kPrecarious:
      return "Precarious";
    case ConnectionQualityLevel::kStable:
      return "Stable";

    case ConnectionQualityLevel::kNumLevels:
      break;
  }
  return "[unknown]";
}

}  // namespace moqt
