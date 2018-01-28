// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A send algorithm that adds pacing on top of an another send algorithm.
// It uses the underlying sender's pacing rate to schedule packets.
// It also takes into consideration the expected granularity of the underlying
// alarm to ensure that alarms are not set too aggressively, and err towards
// sending packets too early instead of too late.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_PACING_SENDER_H_
#define NET_QUIC_CORE_CONGESTION_CONTROL_PACING_SENDER_H_

#include <cstdint>
#include <map>
#include <memory>

#include "base/macros.h"
#include "net/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

namespace test {
class QuicSentPacketManagerPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE PacingSender {
 public:
  PacingSender();
  ~PacingSender();

  // Sets the underlying sender. Does not take ownership of |sender|. |sender|
  // must not be null. This must be called before any of the
  // SendAlgorithmInterface wrapper methods are called.
  void set_sender(SendAlgorithmInterface* sender);

  void set_max_pacing_rate(QuicBandwidth max_pacing_rate) {
    max_pacing_rate_ = max_pacing_rate;
  }

  QuicBandwidth max_pacing_rate() const { return max_pacing_rate_; }

  void OnCongestionEvent(bool rtt_updated,
                         QuicByteCount bytes_in_flight,
                         QuicTime event_time,
                         const AckedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets);

  void OnPacketSent(QuicTime sent_time,
                    QuicByteCount bytes_in_flight,
                    QuicPacketNumber packet_number,
                    QuicByteCount bytes,
                    HasRetransmittableData has_retransmittable_data);

  QuicTime::Delta TimeUntilSend(QuicTime now, QuicByteCount bytes_in_flight);

  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const;

 private:
  friend class test::QuicSentPacketManagerPeer;

  // Underlying sender. Not owned.
  SendAlgorithmInterface* sender_;
  // If not QuicBandidth::Zero, the maximum rate the PacingSender will use.
  QuicBandwidth max_pacing_rate_;

  // Number of unpaced packets to be sent before packets are delayed.
  uint32_t burst_tokens_;
  // Send time of the last packet considered delayed.
  QuicTime last_delayed_packet_sent_time_;
  QuicTime ideal_next_packet_send_time_;  // When can the next packet be sent.
  bool was_last_send_delayed_;  // True when the last send was delayed.
  uint32_t initial_burst_size_;

  DISALLOW_COPY_AND_ASSIGN(PacingSender);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_PACING_SENDER_H_
