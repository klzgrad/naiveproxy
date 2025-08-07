// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_BITRATE_ADJUSTER_H_
#define QUICHE_QUIC_MOQT_MOQT_BITRATE_ADJUSTER_H_

#include <cstdint>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// A sender that can potentially have its outgoing bitrate adjusted.
class BitrateAdjustable {
 public:
  virtual ~BitrateAdjustable() {}

  // Returns the currently used bitrate.
  virtual quic::QuicBandwidth GetCurrentBitrate() const = 0;
  // Adjusts the bitrate to a new target. Returns true if the adjustment was
  // successful.
  virtual bool AdjustBitrate(quic::QuicBandwidth bandwidth) = 0;
};

// MoqtBitrateAdjuster monitors the progress of delivery for a single track, and
// adjusts the bitrate of the track in question accordingly.
class MoqtBitrateAdjuster : public MoqtPublishingMonitorInterface {
 public:
  MoqtBitrateAdjuster(const quic::QuicClock* clock,
                      webtransport::Session* session,
                      BitrateAdjustable* adjustable)
      : clock_(clock),
        session_(session),
        adjustable_(adjustable),
        last_adjustment_time_(clock->ApproximateNow()) {}

  // MoqtPublishingMonitorInterface implementation.
  void OnObjectAckSupportKnown(bool supported) override;
  void OnObjectAckReceived(uint64_t group_id, uint64_t object_id,
                           quic::QuicTimeDelta delta_from_deadline) override;

 private:
  // Attempts adjusting the bitrate down.
  void AttemptAdjustingDown();

  const quic::QuicClock* clock_;    // Not owned.
  webtransport::Session* session_;  // Not owned.
  BitrateAdjustable* adjustable_;   // Not owned.
  quic::QuicTime last_adjustment_time_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_BITRATE_ADJUSTER_H_
