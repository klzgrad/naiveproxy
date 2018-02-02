// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Cubic algorithm, helper class to TCP cubic.
// For details see http://netsrv.csc.ncsu.edu/export/cubic_a_new_tcp_2008.pdf.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_CUBIC_BYTES_H_
#define NET_QUIC_CORE_CONGESTION_CONTROL_CUBIC_BYTES_H_

#include <cstdint>

#include "base/macros.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

namespace test {
class CubicBytesTest;
}  // namespace test

class QUIC_EXPORT_PRIVATE CubicBytes {
 public:
  explicit CubicBytes(const QuicClock* clock);

  void SetNumConnections(int num_connections);

  // Call after a timeout to reset the cubic state.
  void ResetCubicState();

  // Compute a new congestion window to use after a loss event.
  // Returns the new congestion window in packets. The new congestion window is
  // a multiplicative decrease of our current window.
  QuicByteCount CongestionWindowAfterPacketLoss(QuicPacketCount current);

  // Compute a new congestion window to use after a received ACK.
  // Returns the new congestion window in bytes. The new congestion window
  // follows a cubic function that depends on the time passed since last packet
  // loss.
  QuicByteCount CongestionWindowAfterAck(QuicByteCount acked_bytes,
                                         QuicByteCount current,
                                         QuicTime::Delta delay_min,
                                         QuicTime event_time);

  // Call on ack arrival when sender is unable to use the available congestion
  // window. Resets Cubic state during quiescence.
  void OnApplicationLimited();

  // If true, enable the fix for the convex-mode signing bug.  See
  // b/32170105 for more information about the bug.
  // TODO(jokulik):  Remove once the fix is enabled by default.
  void SetFixConvexMode(bool fix_convex_mode);
  // If true, fix CubicBytes quantization bug.  See b/33273459 for
  // more information about the bug.
  // TODO(jokulik): Remove once the fix is enabled by default.
  void SetFixCubicQuantization(bool fix_cubic_quantization);
  // If true, enable the fix for scaling BetaLastMax for n-nonnection
  // emulation.  See b/33272010 for more information about the bug.
  // TODO(jokulik):  Remove once the fix is enabled by default.
  void SetFixBetaLastMax(bool fix_beta_last_max);
  // If true, unconditionally enable each ack to update the congestion
  // window.  See b/33410956 for further information about this bug.
  // TODO(jokulik):  Remove once the fix is enabled by default.
  void SetAllowPerAckUpdates(bool allow_per_ack_updates);

 private:
  friend class test::CubicBytesTest;

  static const QuicTime::Delta MaxCubicTimeInterval() {
    return QuicTime::Delta::FromMilliseconds(30);
  }

  // Compute the TCP Cubic alpha, beta, and beta-last-max based on the
  // current number of connections.
  float Alpha() const;
  float Beta() const;
  float BetaLastMax() const;

  QuicByteCount last_max_congestion_window() const {
    return last_max_congestion_window_;
  }

  const QuicClock* clock_;

  // Number of connections to simulate.
  int num_connections_;

  // Time when this cycle started, after last loss event.
  QuicTime epoch_;

  // Time when we updated last_congestion_window.
  QuicTime last_update_time_;

  // Last congestion window used.
  QuicByteCount last_congestion_window_;

  // Max congestion window used just before last loss event.
  // Note: to improve fairness to other streams an additional back off is
  // applied to this value if the new value is below our latest value.
  QuicByteCount last_max_congestion_window_;

  // Number of acked bytes since the cycle started (epoch).
  QuicByteCount acked_bytes_count_;

  // TCP Reno equivalent congestion window in packets.
  QuicByteCount estimated_tcp_congestion_window_;

  // Origin point of cubic function.
  QuicByteCount origin_point_congestion_window_;

  // Time to origin point of cubic function in 2^10 fractions of a second.
  uint32_t time_to_origin_point_;

  // Last congestion window in packets computed by cubic function.
  QuicByteCount last_target_congestion_window_;

  // Fix convex mode for cubic.
  // TODO(jokulik):  Remove once the cubic convex experiment is done.
  bool fix_convex_mode_;

  // Fix for quantization in cubic mode.
  // TODO(jokulik):  Remove once the experiment is done.
  bool fix_cubic_quantization_;

  // Fix beta last max for n-connection-emulation.
  // TODO(jokulik):  Remove once the corresponding experiment is done.
  bool fix_beta_last_max_;

  // Allow per ack updates, rather than limiting the frequency of
  // updates when in cubic-mode.
  // TODO(jokulik):  Remove once the experiment is done.
  bool allow_per_ack_updates_;

  DISALLOW_COPY_AND_ASSIGN(CubicBytes);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_CUBIC_BYTES_H_
