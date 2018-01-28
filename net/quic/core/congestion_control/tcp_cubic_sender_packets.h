// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// TCP cubic send side congestion algorithm, emulates the behavior of TCP cubic.

#ifndef NET_QUIC_CORE_CONGESTION_CONTROL_TCP_CUBIC_SENDER_PACKETS_H_
#define NET_QUIC_CORE_CONGESTION_CONTROL_TCP_CUBIC_SENDER_PACKETS_H_

#include <cstdint>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/quic/core/congestion_control/cubic.h"
#include "net/quic/core/congestion_control/hybrid_slow_start.h"
#include "net/quic/core/congestion_control/prr_sender.h"
#include "net/quic/core/congestion_control/tcp_cubic_sender_base.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class RttStats;

namespace test {
class TcpCubicSenderPacketsPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE TcpCubicSenderPackets : public TcpCubicSenderBase {
 public:
  // Reno option and max_tcp_congestion_window are provided for testing.
  TcpCubicSenderPackets(const QuicClock* clock,
                        const RttStats* rtt_stats,
                        bool reno,
                        QuicPacketCount initial_tcp_congestion_window,
                        QuicPacketCount max_tcp_congestion_window,
                        QuicConnectionStats* stats);
  ~TcpCubicSenderPackets() override;

  // Start implementation of SendAlgorithmInterface.
  void SetFromConfig(const QuicConfig& config,
                     Perspective perspective) override;
  void SetNumEmulatedConnections(int num_connections) override;
  void OnConnectionMigration() override;
  QuicByteCount GetCongestionWindow() const override;
  QuicByteCount GetSlowStartThreshold() const override;
  CongestionControlType GetCongestionControlType() const override;
  // End implementation of SendAlgorithmInterface.

  QuicByteCount min_congestion_window() const { return min_congestion_window_; }

 protected:
  // TcpCubicSenderBase methods
  void SetCongestionWindowFromBandwidthAndRtt(QuicBandwidth bandwidth,
                                              QuicTime::Delta rtt) override;
  void SetCongestionWindowInPackets(QuicPacketCount congestion_window) override;
  void SetMinCongestionWindowInPackets(
      QuicPacketCount congestion_window) override;
  void ExitSlowstart() override;
  void OnPacketLost(QuicPacketNumber largest_loss,
                    QuicByteCount lost_bytes,
                    QuicByteCount prior_in_flight) override;
  void MaybeIncreaseCwnd(QuicPacketNumber acked_packet_number,
                         QuicByteCount acked_bytes,
                         QuicByteCount prior_in_flight,
                         QuicTime event_time) override;
  void HandleRetransmissionTimeout() override;

 private:
  friend class test::TcpCubicSenderPacketsPeer;

  Cubic cubic_;

  // ACK counter for the Reno implementation.
  uint64_t congestion_window_count_;

  // Congestion window in packets.
  QuicPacketCount congestion_window_;

  // Minimum congestion window in packets.
  QuicPacketCount min_congestion_window_;

  // Slow start congestion window in packets, aka ssthresh.
  QuicPacketCount slowstart_threshold_;

  // Maximum number of outstanding packets for tcp.
  QuicPacketCount max_tcp_congestion_window_;

  // Initial TCP congestion window. This variable can only be set when this
  // algorithm is created.
  const QuicPacketCount initial_tcp_congestion_window_;

  // Initial maximum TCP congestion window. This variable can only be set when
  // this algorithm is created.
  const QuicPacketCount initial_max_tcp_congestion_window_;

  // The minimum window when exiting slow start with large reduction.
  QuicPacketCount min_slow_start_exit_window_;

  DISALLOW_COPY_AND_ASSIGN(TcpCubicSenderPackets);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CONGESTION_CONTROL_TCP_CUBIC_SENDER_PACKETS_H_
