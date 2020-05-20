// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The pure virtual class for send side congestion control algorithm.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_SEND_ALGORITHM_INTERFACE_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_SEND_ALGORITHM_INTERFACE_H_

#include <algorithm>
#include <map>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_clock.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_unacked_packet_map.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

typedef uint64_t QuicRoundTripCount;

class CachedNetworkParameters;
class RttStats;

class QUIC_EXPORT_PRIVATE SendAlgorithmInterface {
 public:
  // Network Params for AdjustNetworkParameters.
  struct QUIC_NO_EXPORT NetworkParams {
    NetworkParams()
        : NetworkParams(QuicBandwidth::Zero(), QuicTime::Delta::Zero(), false) {
    }
    NetworkParams(const QuicBandwidth& bandwidth,
                  const QuicTime::Delta& rtt,
                  bool allow_cwnd_to_decrease)
        : bandwidth(bandwidth),
          rtt(rtt),
          allow_cwnd_to_decrease(allow_cwnd_to_decrease),
          quic_fix_bbr_cwnd_in_bandwidth_resumption(
              GetQuicReloadableFlag(quic_fix_bbr_cwnd_in_bandwidth_resumption)),
          quic_bbr_fix_pacing_rate(
              GetQuicReloadableFlag(quic_bbr_fix_pacing_rate)),
          quic_bbr_donot_inject_bandwidth(
              GetQuicReloadableFlag(quic_bbr_donot_inject_bandwidth)) {}

    bool operator==(const NetworkParams& other) const {
      return bandwidth == other.bandwidth && rtt == other.rtt &&
             allow_cwnd_to_decrease == other.allow_cwnd_to_decrease &&
             quic_fix_bbr_cwnd_in_bandwidth_resumption ==
                 other.quic_fix_bbr_cwnd_in_bandwidth_resumption &&
             quic_bbr_fix_pacing_rate == other.quic_bbr_fix_pacing_rate &&
             quic_bbr_donot_inject_bandwidth ==
                 other.quic_bbr_donot_inject_bandwidth;
    }

    QuicBandwidth bandwidth;
    QuicTime::Delta rtt;
    bool allow_cwnd_to_decrease;
    // Code changes that are controlled by flags.
    // TODO(b/131899599): Remove after impact of fix is measured.
    bool quic_fix_bbr_cwnd_in_bandwidth_resumption;
    // TODO(b/143540157): Remove after impact of fix is measured.
    bool quic_bbr_fix_pacing_rate;
    // TODO(b/72089315, b/143891040): Remove after impact of fix is measured.
    bool quic_bbr_donot_inject_bandwidth;
  };

  static SendAlgorithmInterface* Create(
      const QuicClock* clock,
      const RttStats* rtt_stats,
      const QuicUnackedPacketMap* unacked_packets,
      CongestionControlType type,
      QuicRandom* random,
      QuicConnectionStats* stats,
      QuicPacketCount initial_congestion_window,
      SendAlgorithmInterface* old_send_algorithm);

  virtual ~SendAlgorithmInterface() {}

  virtual void SetFromConfig(const QuicConfig& config,
                             Perspective perspective) = 0;

  // Sets the initial congestion window in number of packets.  May be ignored
  // if called after the initial congestion window is no longer relevant.
  virtual void SetInitialCongestionWindowInPackets(QuicPacketCount packets) = 0;

  // Indicates an update to the congestion state, caused either by an incoming
  // ack or loss event timeout.  |rtt_updated| indicates whether a new
  // latest_rtt sample has been taken, |prior_in_flight| the bytes in flight
  // prior to the congestion event.  |acked_packets| and |lost_packets| are any
  // packets considered acked or lost as a result of the congestion event.
  virtual void OnCongestionEvent(bool rtt_updated,
                                 QuicByteCount prior_in_flight,
                                 QuicTime event_time,
                                 const AckedPacketVector& acked_packets,
                                 const LostPacketVector& lost_packets) = 0;

  // Inform that we sent |bytes| to the wire, and if the packet is
  // retransmittable.  |bytes_in_flight| is the number of bytes in flight before
  // the packet was sent.
  // Note: this function must be called for every packet sent to the wire.
  virtual void OnPacketSent(QuicTime sent_time,
                            QuicByteCount bytes_in_flight,
                            QuicPacketNumber packet_number,
                            QuicByteCount bytes,
                            HasRetransmittableData is_retransmittable) = 0;

  // Inform that |packet_number| has been neutered.
  virtual void OnPacketNeutered(QuicPacketNumber packet_number) = 0;

  // Called when the retransmission timeout fires.  Neither OnPacketAbandoned
  // nor OnPacketLost will be called for these packets.
  virtual void OnRetransmissionTimeout(bool packets_retransmitted) = 0;

  // Called when connection migrates and cwnd needs to be reset.
  virtual void OnConnectionMigration() = 0;

  // Make decision on whether the sender can send right now.  Note that even
  // when this method returns true, the sending can be delayed due to pacing.
  virtual bool CanSend(QuicByteCount bytes_in_flight) = 0;

  // The pacing rate of the send algorithm.  May be zero if the rate is unknown.
  virtual QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const = 0;

  // What's the current estimated bandwidth in bytes per second.
  // Returns 0 when it does not have an estimate.
  virtual QuicBandwidth BandwidthEstimate() const = 0;

  // Returns the size of the current congestion window in bytes.  Note, this is
  // not the *available* window.  Some send algorithms may not use a congestion
  // window and will return 0.
  virtual QuicByteCount GetCongestionWindow() const = 0;

  // Whether the send algorithm is currently in slow start.  When true, the
  // BandwidthEstimate is expected to be too low.
  virtual bool InSlowStart() const = 0;

  // Whether the send algorithm is currently in recovery.
  virtual bool InRecovery() const = 0;

  // True when the congestion control is probing for more bandwidth and needs
  // enough data to not be app-limited to do so.
  // TODO(ianswett): In the future, this API may want to indicate the size of
  // the probing packet.
  virtual bool ShouldSendProbingPacket() const = 0;

  // Returns the size of the slow start congestion window in bytes,
  // aka ssthresh.  Only defined for Cubic and Reno, other algorithms return 0.
  virtual QuicByteCount GetSlowStartThreshold() const = 0;

  virtual CongestionControlType GetCongestionControlType() const = 0;

  // Notifies the congestion control algorithm of an external network
  // measurement or prediction.  Either |bandwidth| or |rtt| may be zero if no
  // sample is available.
  virtual void AdjustNetworkParameters(const NetworkParams& params) = 0;

  // Retrieves debugging information about the current state of the
  // send algorithm.
  virtual std::string GetDebugState() const = 0;

  // Called when the connection has no outstanding data to send. Specifically,
  // this means that none of the data streams are write-blocked, there are no
  // packets in the connection queue, and there are no pending retransmissins,
  // i.e. the sender cannot send anything for reasons other than being blocked
  // by congestion controller. This includes cases when the connection is
  // blocked by the flow controller.
  //
  // The fact that this method is called does not necessarily imply that the
  // connection would not be blocked by the congestion control if it actually
  // tried to send data. If the congestion control algorithm needs to exclude
  // such cases, it should use the internal state it uses for congestion control
  // for that.
  virtual void OnApplicationLimited(QuicByteCount bytes_in_flight) = 0;

  // Called before connection close to collect stats.
  virtual void PopulateConnectionStats(QuicConnectionStats* stats) const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_SEND_ALGORITHM_INTERFACE_H_
