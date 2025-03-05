// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A send algorithm that adds pacing on top of an another send algorithm.
// It uses the underlying sender's pacing rate to schedule packets.
// It also takes into consideration the expected granularity of the underlying
// alarm to ensure that alarms are not set too aggressively, and err towards
// sending packets too early instead of too late.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_PACING_SENDER_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_PACING_SENDER_H_

#include <cstdint>
#include <map>
#include <memory>

#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicSentPacketManagerPeer;
}  // namespace test

class QUICHE_EXPORT PacingSender {
 public:
  PacingSender();
  PacingSender(const PacingSender&) = delete;
  PacingSender& operator=(const PacingSender&) = delete;
  ~PacingSender();

  // Sets the underlying sender. Does not take ownership of |sender|. |sender|
  // must not be null. This must be called before any of the
  // SendAlgorithmInterface wrapper methods are called.
  void set_sender(SendAlgorithmInterface* sender);

  void set_max_pacing_rate(QuicBandwidth max_pacing_rate) {
    max_pacing_rate_ = max_pacing_rate;
  }

  void set_application_driven_pacing_rate(
      QuicBandwidth application_driven_pacing_rate) {
    // Soft pacing suggestion from application layer. Experimental, see
    // b/364614652 for more context.

    application_driven_pacing_rate_ = application_driven_pacing_rate;
    sender_->SetApplicationDrivenPacingRate(application_driven_pacing_rate_);
  }

  QuicBandwidth application_driven_pacing_rate() const {
    return application_driven_pacing_rate_;
  }

  void set_remove_non_initial_burst() { remove_non_initial_burst_ = true; }

  QuicBandwidth max_pacing_rate() const { return max_pacing_rate_; }

  void OnCongestionEvent(bool rtt_updated, QuicByteCount bytes_in_flight,
                         QuicTime event_time,
                         const AckedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets,
                         QuicPacketCount num_ect, QuicPacketCount num_ce);

  void OnPacketSent(QuicTime sent_time, QuicByteCount bytes_in_flight,
                    QuicPacketNumber packet_number, QuicByteCount bytes,
                    HasRetransmittableData has_retransmittable_data);

  // Called when application throttles the sending, so that pacing sender stops
  // making up for lost time.
  void OnApplicationLimited();

  // Set burst_tokens_ and initial_burst_size_.
  void SetBurstTokens(uint32_t burst_tokens);

  QuicTime::Delta TimeUntilSend(QuicTime now,
                                QuicByteCount bytes_in_flight) const;

  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const;

  NextReleaseTimeResult GetNextReleaseTime() const {
    bool allow_burst = (burst_tokens_ > 0 || lumpy_tokens_ > 0);
    return {ideal_next_packet_send_time_, allow_burst};
  }

  uint32_t initial_burst_size() const { return initial_burst_size_; }

 protected:
  uint32_t lumpy_tokens() const { return lumpy_tokens_; }

 private:
  friend class test::QuicSentPacketManagerPeer;

  // Underlying sender. Not owned.
  SendAlgorithmInterface* sender_;
  // If not QuicBandidth::Zero, the maximum rate the PacingSender will use.
  QuicBandwidth max_pacing_rate_;

  // Keep track of the application driven pacing rate used by sender_.
  // Experimental, see b/364614652 for more context. This signals application
  // bandwidth needs to the underlying BBR sender, so that we can back off
  // accordingly when congestion is detected.
  QuicBandwidth application_driven_pacing_rate_;

  // Number of unpaced packets to be sent before packets are delayed.
  uint32_t burst_tokens_;
  QuicTime ideal_next_packet_send_time_;  // When can the next packet be sent.
  uint32_t initial_burst_size_;

  // Number of unpaced packets to be sent before packets are delayed. This token
  // is consumed after burst_tokens_ ran out.
  uint32_t lumpy_tokens_;

  // Indicates whether pacing throttles the sending. If true, make up for lost
  // time.
  bool pacing_limited_;

  bool remove_non_initial_burst_ =
      GetQuicReloadableFlag(quic_pacing_remove_non_initial_burst);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_PACING_SENDER_H_
