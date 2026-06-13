// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_BITRATE_ADJUSTER_H_
#define QUICHE_QUIC_MOQT_MOQT_BITRATE_ADJUSTER_H_

#include <memory>
#include <optional>
#include <string>

#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_outstanding_objects.h"
#include "quiche/quic/moqt/moqt_probe_manager.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// Indicates the type of new bitrate estimate.
enum class BitrateAdjustmentType {
  // Indicates that the sender is sending too much data.
  kDown,

  // Indicates that the sender should attempt to increase the amount of data
  // sent.
  kUp,
};

// A sender that can potentially have its outgoing bitrate adjusted.
class BitrateAdjustable {
 public:
  virtual ~BitrateAdjustable() {}

  // Returns the currently used bitrate.
  // TODO(vasilvv): we should not depend on this value long-term, since the
  // self-reported bitrate is not reliable in most real encoders.
  virtual quic::QuicBandwidth GetCurrentBitrate() const = 0;

  // Returns true if the sender could make use of more bandwidth than it is
  // currently sending at.
  virtual bool CouldUseExtraBandwidth() = 0;

  // Notifies the sender that it should consider increasing or decreasing its
  // bandwidth.  `bandwidth` is the estimate of bandwidth available to the
  // application.
  virtual void ConsiderAdjustingBitrate(quic::QuicBandwidth bandwidth,
                                        BitrateAdjustmentType type) = 0;
};

// Connection quality levels.  Used by the bitrate adjuster to decide when to
// probe up or down.
enum class ConnectionQualityLevel {
  // Lowest quality level.  Reaching it will result in the bitrate adjuster
  // attempting to immediately lower the bitrate.
  kCritical,
  // At this level, no new bandwidth probes will be started and all existing
  // bandwidth probes are cancelled.
  kVeryPrecarious,
  // At this level, the adjuster will not start any new probes, but all of the
  // previous probes will be allowed to continue.  This level is separate from
  // the previous one to avoid unstable behavior around the boundaries.
  kPrecarious,
  // Highest quality level.  The adjuster will attempt to probe bandwidth if
  // necessary.
  kStable,

  kNumLevels,
};

std::string ConnectionQualityLevelToString(ConnectionQualityLevel level);
template <typename Sink>
void AbslStringify(Sink& sink, ConnectionQualityLevel level) {
  sink.Append(ConnectionQualityLevelToString(level));
}

inline constexpr int kNumQualityLevels =
    static_cast<int>(ConnectionQualityLevel::kNumLevels);

// Parameters (mostly magic numbers) that determine behavior of
// MoqtBitrateAdjuster.
struct MoqtBitrateAdjusterParameters {
  // When bitrate is adjusted down, multiply the congestion controller estimate
  // by this factor.  This should be less than 1, since congestion controller
  // estimate tends to be overly optimistic in practice.
  float target_bitrate_multiplier_down = 0.9f;

  // Same, but applies for adjusting up.  Similar considerations for selecting
  // the value apply.
  float target_bitrate_multiplier_up = 0.9f;

  // Do not perform any updates within `initial_delay` after the connection
  // start.
  quic::QuicTimeDelta initial_delay = quic::QuicTimeDelta::FromSeconds(2);

  // Quality level thresholds.  If the object arrives with the
  // time-before-deadline that is lower than the first number listed here, it
  // will result in the connection being assigned the lowest quality level, and
  // so on.  The thresholds are expressed as a fraction of `time_window` (which
  // typically would be equal to the size of the buffer in seconds).
  float quality_level_time_thresholds[kNumQualityLevels - 1] = {0.2f, 0.4f,
                                                                0.6f};

  // The maximum gap between the next object expected to be received, and the
  // actually received object, expressed as a number of objects. If the
  // reordering gap exceeds the configured threshold, the connection is marked
  // as being in that quality level. The thresholds correspond to kCritical,
  // kVeryPrecarious, and kPrecarious.
  //
  // The default for the worst-case reordering is 12, which corresponds to about
  // 400ms for 30fps video.
  int quality_level_reordering_thresholds[kNumQualityLevels - 1] = {12, 6, 3};

  // Amount of time the connection has to spend in good state before attempting
  // to probe for bandwidth.
  quic::QuicTimeDelta time_before_probing = quic::QuicTimeDelta::FromSeconds(2);

  // When probing, attempt to increase the bandwidth by the specified factor.
  // Used when determining the probe size and timeout.
  float probe_increase_target = 1.2;

  // Expected duration of a probe, expressed in the number of round-trips
  // necessary. Selecting values below 8 might interact negatively with BBR.
  float round_trips_for_probe = 16;

  // Probe results will be ignored if the probe was cancelled before lasting for
  // the specified duration.
  float min_probe_duration_in_rtts = 4;
};

// MoqtBitrateAdjuster monitors the progress of delivery for a single track, and
// adjusts the bitrate of the track in question accordingly.
class MoqtBitrateAdjuster : public MoqtPublishingMonitorInterface {
 public:
  MoqtBitrateAdjuster(const quic::QuicClock* clock,
                      webtransport::Session* session,
                      quic::QuicAlarmFactory* alarm_factory,
                      BitrateAdjustable* adjustable);
  MoqtBitrateAdjuster(const quic::QuicClock* clock,
                      webtransport::Session* session,
                      std::unique_ptr<MoqtProbeManagerInterface> probe_manager,
                      BitrateAdjustable* adjustable);

  // MoqtPublishingMonitorInterface implementation.
  void OnObjectAckSupportKnown(
      std::optional<quic::QuicTimeDelta> time_window) override;
  void OnNewObjectEnqueued(Location location) override;
  void OnObjectAckReceived(Location location,
                           quic::QuicTimeDelta delta_from_deadline) override;

  MoqtTraceRecorder& trace_recorder() { return trace_recorder_; }
  MoqtBitrateAdjusterParameters& parameters() { return parameters_; }

 private:
  void Start();

  // Checks if the bitrate adjuster should react to an individual ack.
  bool ShouldUseAckAsActionSignal(Location location);

  // Checks if the bitrate should be adjusted down based on the result of
  // processing an object ACK.
  ConnectionQualityLevel GetQualityLevel(
      int reordering_delta, quic::QuicTimeDelta delta_from_deadline) const;

  // Starts a bandwidth probe if all the conditions are met.
  void MaybeStartProbing(quic::QuicTime now);
  // Callback called whenever the bandwidth probe is finished.
  void OnProbeResult(const webtransport::SessionStats& old_stats,
                     const ProbeResult& result);

  // Attempts adjusting the bitrate down.
  void AttemptAdjustingDown();

  void SuggestNewBitrate(quic::QuicBandwidth bitrate,
                         BitrateAdjustmentType type);

  const quic::QuicClock* clock_;    // Not owned.
  webtransport::Session* session_;  // Not owned.
  BitrateAdjustable* adjustable_;   // Not owned.
  MoqtTraceRecorder trace_recorder_;
  MoqtBitrateAdjusterParameters parameters_;

  // The time at which Start() has been called.
  quic::QuicTime start_time_ = quic::QuicTime::Zero();

  // The window size received from the peer.  This amount is used to establish
  // the scale for incoming time deltas in the object ACKs.
  quic::QuicTimeDelta time_window_ = quic::QuicTimeDelta::Zero();

  // The earliest point at which the connection has been continuously in the
  // stable state.  Used to time bandwidth probes.
  std::optional<quic::QuicTime> in_stable_state_since_;

  std::optional<MoqtOutstandingObjects> outstanding_objects_;
  std::unique_ptr<MoqtProbeManagerInterface> probe_manager_;
  Location last_acked_object_;
};

// Given a suggestion to change bitrate `old_bitrate` to `new_bitrate` with the
// specified adjustment type, returns true if the change should be ignored.
// `min_change` is the threshold below which the change should be ignored,
// specified as a fraction of old bitrate.
bool ShouldIgnoreBitrateAdjustment(quic::QuicBandwidth new_bitrate,
                                   BitrateAdjustmentType type,
                                   quic::QuicBandwidth old_bitrate,
                                   float min_change);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_BITRATE_ADJUSTER_H_
