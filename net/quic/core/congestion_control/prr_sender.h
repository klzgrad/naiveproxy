// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Implements Proportional Rate Reduction (PRR) per RFC 6937.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_PRR_SENDER_H_
#define NET_QUIC_CORE_CONGESTION_CONTROL_PRR_SENDER_H_

#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class QUIC_EXPORT_PRIVATE PrrSender {
 public:
  PrrSender();
  // OnPacketLost should be called on the first loss that triggers a recovery
  // period and all other methods in this class should only be called when in
  // recovery.
  void OnPacketLost(QuicByteCount prior_in_flight);
  void OnPacketSent(QuicByteCount sent_bytes);
  void OnPacketAcked(QuicByteCount acked_bytes);
  bool CanSend(QuicByteCount congestion_window,
               QuicByteCount bytes_in_flight,
               QuicByteCount slowstart_threshold) const;

 private:
  // Bytes sent and acked since the last loss event.
  // |bytes_sent_since_loss_| is the same as "prr_out_" in RFC 6937,
  // and |bytes_delivered_since_loss_| is the same as "prr_delivered_".
  QuicByteCount bytes_sent_since_loss_;
  QuicByteCount bytes_delivered_since_loss_;
  size_t ack_count_since_loss_;

  // The congestion window before the last loss event.
  QuicByteCount bytes_in_flight_before_loss_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_PRR_SENDER_H_
