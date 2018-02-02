// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/congestion_control/tcp_cubic_sender_base.h"

#include <algorithm>

#include "net/quic/core/congestion_control/prr_sender.h"
#include "net/quic/core/congestion_control/rtt_stats.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/proto/cached_network_parameters.pb.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

namespace net {

namespace {
// Constants based on TCP defaults.
// The minimum cwnd based on RFC 3782 (TCP NewReno) for cwnd reductions on a
// fast retransmission.  The cwnd after a timeout is still 1.
const QuicByteCount kMaxBurstBytes = 3 * kDefaultTCPMSS;
const float kRenoBeta = 0.7f;               // Reno backoff factor.
const uint32_t kDefaultNumConnections = 2;  // N-connection emulation.
}  // namespace

TcpCubicSenderBase::TcpCubicSenderBase(const QuicClock* clock,
                                       const RttStats* rtt_stats,
                                       bool reno,
                                       QuicConnectionStats* stats)
    : rtt_stats_(rtt_stats),
      stats_(stats),
      reno_(reno),
      num_connections_(kDefaultNumConnections),
      largest_sent_packet_number_(0),
      largest_acked_packet_number_(0),
      largest_sent_at_last_cutback_(0),
      min4_mode_(false),
      last_cutback_exited_slowstart_(false),
      slow_start_large_reduction_(false),
      no_prr_(false) {}

TcpCubicSenderBase::~TcpCubicSenderBase() {}

void TcpCubicSenderBase::SetFromConfig(const QuicConfig& config,
                                       Perspective perspective) {
  if (perspective == Perspective::IS_SERVER) {
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kIW03)) {
      // Initial window experiment.
      SetCongestionWindowInPackets(3);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kIW10)) {
      // Initial window experiment.
      SetCongestionWindowInPackets(10);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kIW20)) {
      // Initial window experiment.
      SetCongestionWindowInPackets(20);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kIW50)) {
      // Initial window experiment.
      SetCongestionWindowInPackets(50);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kMIN1)) {
      // Min CWND experiment.
      SetMinCongestionWindowInPackets(1);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kMIN4)) {
      // Min CWND of 4 experiment.
      min4_mode_ = true;
      SetMinCongestionWindowInPackets(1);
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kSSLR)) {
      // Slow Start Fast Exit experiment.
      slow_start_large_reduction_ = true;
    }
    if (config.HasReceivedConnectionOptions() &&
        ContainsQuicTag(config.ReceivedConnectionOptions(), kNPRR)) {
      // Use unity pacing instead of PRR.
      no_prr_ = true;
    }
  }
}

void TcpCubicSenderBase::AdjustNetworkParameters(QuicBandwidth bandwidth,
                                                 QuicTime::Delta rtt) {
  if (bandwidth.IsZero() || rtt.IsZero()) {
    return;
  }

  SetCongestionWindowFromBandwidthAndRtt(bandwidth, rtt);
}

void TcpCubicSenderBase::SetNumEmulatedConnections(int num_connections) {
  num_connections_ = std::max(1, num_connections);
}

float TcpCubicSenderBase::RenoBeta() const {
  // kNConnectionBeta is the backoff factor after loss for our N-connection
  // emulation, which emulates the effective backoff of an ensemble of N
  // TCP-Reno connections on a single loss event. The effective multiplier is
  // computed as:
  return (num_connections_ - 1 + kRenoBeta) / num_connections_;
}

void TcpCubicSenderBase::OnCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    QuicTime event_time,
    const AckedPacketVector& acked_packets,
    const LostPacketVector& lost_packets) {
  if (rtt_updated && InSlowStart() &&
      hybrid_slow_start_.ShouldExitSlowStart(
          rtt_stats_->latest_rtt(), rtt_stats_->min_rtt(),
          GetCongestionWindow() / kDefaultTCPMSS)) {
    ExitSlowstart();
  }
  for (const LostPacket& lost_packet : lost_packets) {
    OnPacketLost(lost_packet.packet_number, lost_packet.bytes_lost,
                 prior_in_flight);
  }
  for (const AckedPacket acked_packet : acked_packets) {
    OnPacketAcked(acked_packet.packet_number, acked_packet.bytes_acked,
                  prior_in_flight, event_time);
  }
}

void TcpCubicSenderBase::OnPacketAcked(QuicPacketNumber acked_packet_number,
                                       QuicByteCount acked_bytes,
                                       QuicByteCount prior_in_flight,
                                       QuicTime event_time) {
  largest_acked_packet_number_ =
      std::max(acked_packet_number, largest_acked_packet_number_);
  if (InRecovery()) {
    if (!no_prr_) {
      // PRR is used when in recovery.
      prr_.OnPacketAcked(acked_bytes);
    }
    return;
  }
  MaybeIncreaseCwnd(acked_packet_number, acked_bytes, prior_in_flight,
                    event_time);
  if (InSlowStart()) {
    hybrid_slow_start_.OnPacketAcked(acked_packet_number);
  }
}

void TcpCubicSenderBase::OnPacketSent(
    QuicTime /*sent_time*/,
    QuicByteCount /*bytes_in_flight*/,
    QuicPacketNumber packet_number,
    QuicByteCount bytes,
    HasRetransmittableData is_retransmittable) {
  if (InSlowStart()) {
    ++(stats_->slowstart_packets_sent);
  }

  if (is_retransmittable != HAS_RETRANSMITTABLE_DATA) {
    return;
  }
  if (InRecovery()) {
    // PRR is used when in recovery.
    prr_.OnPacketSent(bytes);
  }
  DCHECK_LT(largest_sent_packet_number_, packet_number);
  largest_sent_packet_number_ = packet_number;
  hybrid_slow_start_.OnPacketSent(packet_number);
}

bool TcpCubicSenderBase::CanSend(QuicByteCount bytes_in_flight) {
  if (!no_prr_ && InRecovery()) {
    // PRR is used when in recovery.
    return prr_.CanSend(GetCongestionWindow(), bytes_in_flight,
                        GetSlowStartThreshold());
  }
  if (GetCongestionWindow() > bytes_in_flight) {
    return true;
  }
  if (min4_mode_ && bytes_in_flight < 4 * kDefaultTCPMSS) {
    return true;
  }
  return false;
}

QuicBandwidth TcpCubicSenderBase::PacingRate(
    QuicByteCount /* bytes_in_flight */) const {
  // We pace at twice the rate of the underlying sender's bandwidth estimate
  // during slow start and 1.25x during congestion avoidance to ensure pacing
  // doesn't prevent us from filling the window.
  QuicTime::Delta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    srtt = QuicTime::Delta::FromMicroseconds(rtt_stats_->initial_rtt_us());
  }
  const QuicBandwidth bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
  return bandwidth * (InSlowStart() ? 2 : (no_prr_ && InRecovery() ? 1 : 1.25));
}

QuicBandwidth TcpCubicSenderBase::BandwidthEstimate() const {
  QuicTime::Delta srtt = rtt_stats_->smoothed_rtt();
  if (srtt.IsZero()) {
    // If we haven't measured an rtt, the bandwidth estimate is unknown.
    return QuicBandwidth::Zero();
  }
  return QuicBandwidth::FromBytesAndTimeDelta(GetCongestionWindow(), srtt);
}

bool TcpCubicSenderBase::InSlowStart() const {
  return GetCongestionWindow() < GetSlowStartThreshold();
}

bool TcpCubicSenderBase::IsCwndLimited(QuicByteCount bytes_in_flight) const {
  const QuicByteCount congestion_window = GetCongestionWindow();
  if (bytes_in_flight >= congestion_window) {
    return true;
  }
  const QuicByteCount available_bytes = congestion_window - bytes_in_flight;
  const bool slow_start_limited =
      InSlowStart() && bytes_in_flight > congestion_window / 2;
  return slow_start_limited || available_bytes <= kMaxBurstBytes;
}

bool TcpCubicSenderBase::InRecovery() const {
  return largest_acked_packet_number_ <= largest_sent_at_last_cutback_ &&
         largest_acked_packet_number_ != 0;
}

bool TcpCubicSenderBase::IsProbingForMoreBandwidth() const {
  return false;
}

void TcpCubicSenderBase::OnRetransmissionTimeout(bool packets_retransmitted) {
  largest_sent_at_last_cutback_ = 0;
  if (!packets_retransmitted) {
    return;
  }
  hybrid_slow_start_.Restart();
  HandleRetransmissionTimeout();
}

void TcpCubicSenderBase::OnConnectionMigration() {
  hybrid_slow_start_.Restart();
  prr_ = PrrSender();
  largest_sent_packet_number_ = 0;
  largest_acked_packet_number_ = 0;
  largest_sent_at_last_cutback_ = 0;
  last_cutback_exited_slowstart_ = false;
}

std::string TcpCubicSenderBase::GetDebugState() const {
  return "";
}

void TcpCubicSenderBase::OnApplicationLimited(QuicByteCount bytes_in_flight) {}

}  // namespace net
