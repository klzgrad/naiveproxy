// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BANDWIDTH_SAMPLER_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BANDWIDTH_SAMPLER_H_

#include "net/third_party/quiche/src/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/windowed_filter.h"
#include "net/third_party/quiche/src/quic/core/packet_number_indexed_queue.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_unacked_packet_map.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

namespace test {
class BandwidthSamplerPeer;
}  // namespace test

// A subset of BandwidthSampler::ConnectionStateOnSentPacket which is returned
// to the caller when the packet is acked or lost.
struct QUIC_EXPORT_PRIVATE SendTimeState {
  SendTimeState()
      : is_valid(false),
        is_app_limited(false),
        total_bytes_sent(0),
        total_bytes_acked(0),
        total_bytes_lost(0),
        bytes_in_flight(0) {}

  SendTimeState(bool is_app_limited,
                QuicByteCount total_bytes_sent,
                QuicByteCount total_bytes_acked,
                QuicByteCount total_bytes_lost,
                QuicByteCount bytes_in_flight)
      : is_valid(true),
        is_app_limited(is_app_limited),
        total_bytes_sent(total_bytes_sent),
        total_bytes_acked(total_bytes_acked),
        total_bytes_lost(total_bytes_lost),
        bytes_in_flight(bytes_in_flight) {}

  SendTimeState(const SendTimeState& other) = default;

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const SendTimeState& s);

  // Whether other states in this object is valid.
  bool is_valid;

  // Whether the sender is app limited at the time the packet was sent.
  // App limited bandwidth sample might be artificially low because the sender
  // did not have enough data to send in order to saturate the link.
  bool is_app_limited;

  // Total number of sent bytes at the time the packet was sent.
  // Includes the packet itself.
  QuicByteCount total_bytes_sent;

  // Total number of acked bytes at the time the packet was sent.
  QuicByteCount total_bytes_acked;

  // Total number of lost bytes at the time the packet was sent.
  QuicByteCount total_bytes_lost;

  // Total number of inflight bytes at the time the packet was sent.
  // Includes the packet itself.
  // It should be equal to |total_bytes_sent| minus the sum of
  // |total_bytes_acked|, |total_bytes_lost| and total neutered bytes.
  QuicByteCount bytes_in_flight;
};

struct QUIC_EXPORT_PRIVATE BandwidthSample {
  // The bandwidth at that particular sample. Zero if no valid bandwidth sample
  // is available.
  QuicBandwidth bandwidth;

  // The RTT measurement at this particular sample.  Zero if no RTT sample is
  // available.  Does not correct for delayed ack time.
  QuicTime::Delta rtt;

  // States captured when the packet was sent.
  SendTimeState state_at_send;

  BandwidthSample()
      : bandwidth(QuicBandwidth::Zero()), rtt(QuicTime::Delta::Zero()) {}
};

// MaxAckHeightTracker is part of the BandwidthSampler. It is called after every
// ack event to keep track the degree of ack aggregation(a.k.a "ack height").
class QUIC_EXPORT_PRIVATE MaxAckHeightTracker {
 public:
  explicit MaxAckHeightTracker(QuicRoundTripCount initial_filter_window)
      : max_ack_height_filter_(initial_filter_window, 0, 0) {}

  QuicByteCount Get() const { return max_ack_height_filter_.GetBest(); }

  QuicByteCount Update(QuicBandwidth bandwidth_estimate,
                       QuicRoundTripCount round_trip_count,
                       QuicTime ack_time,
                       QuicByteCount bytes_acked);

  void SetFilterWindowLength(QuicRoundTripCount length) {
    max_ack_height_filter_.SetWindowLength(length);
  }

  void Reset(QuicByteCount new_height, QuicRoundTripCount new_time) {
    max_ack_height_filter_.Reset(new_height, new_time);
  }

  void SetAckAggregationBandwidthThreshold(double threshold) {
    ack_aggregation_bandwidth_threshold_ = threshold;
  }

  double ack_aggregation_bandwidth_threshold() const {
    return ack_aggregation_bandwidth_threshold_;
  }

  uint64_t num_ack_aggregation_epochs() const {
    return num_ack_aggregation_epochs_;
  }

 private:
  // Tracks the maximum number of bytes acked faster than the estimated
  // bandwidth.
  typedef WindowedFilter<QuicByteCount,
                         MaxFilter<QuicByteCount>,
                         QuicRoundTripCount,
                         QuicRoundTripCount>
      MaxAckHeightFilter;
  MaxAckHeightFilter max_ack_height_filter_;

  // The time this aggregation started and the number of bytes acked during it.
  QuicTime aggregation_epoch_start_time_ = QuicTime::Zero();
  QuicByteCount aggregation_epoch_bytes_ = 0;
  // The number of ack aggregation epochs ever started, including the ongoing
  // one. Stats only.
  uint64_t num_ack_aggregation_epochs_ = 0;
  double ack_aggregation_bandwidth_threshold_ =
      GetQuicFlag(FLAGS_quic_ack_aggregation_bandwidth_threshold);
};

// An interface common to any class that can provide bandwidth samples from the
// information per individual acknowledged packet.
class QUIC_EXPORT_PRIVATE BandwidthSamplerInterface {
 public:
  virtual ~BandwidthSamplerInterface() {}

  // Inputs the sent packet information into the sampler. Assumes that all
  // packets are sent in order. The information about the packet will not be
  // released from the sampler until it the packet is either acknowledged or
  // declared lost.
  virtual void OnPacketSent(
      QuicTime sent_time,
      QuicPacketNumber packet_number,
      QuicByteCount bytes,
      QuicByteCount bytes_in_flight,
      HasRetransmittableData has_retransmittable_data) = 0;

  virtual void OnPacketNeutered(QuicPacketNumber packet_number) = 0;

  struct QUIC_NO_EXPORT CongestionEventSample {
    // The maximum bandwidth sample from all acked packets.
    // QuicBandwidth::Zero() if no samples are available.
    QuicBandwidth sample_max_bandwidth = QuicBandwidth::Zero();
    // Whether |sample_max_bandwidth| is from a app-limited sample.
    bool sample_is_app_limited = false;
    // The minimum rtt sample from all acked packets.
    // QuicTime::Delta::Infinite() if no samples are available.
    QuicTime::Delta sample_rtt = QuicTime::Delta::Infinite();
    // For each packet p in acked packets, this is the max value of INFLIGHT(p),
    // where INFLIGHT(p) is the number of bytes acked while p is inflight.
    QuicByteCount sample_max_inflight = 0;
    // The send state of the largest packet in acked_packets, unless it is
    // empty. If acked_packets is empty, it's the send state of the largest
    // packet in lost_packets.
    SendTimeState last_packet_send_state;
    // The number of extra bytes acked from this ack event, compared to what is
    // expected from the flow's bandwidth. Larger value means more ack
    // aggregation.
    QuicByteCount extra_acked = 0;
  };
  // Notifies the sampler that at |ack_time|, all packets in |acked_packets|
  // have been acked, and all packets in |lost_packets| have been lost.
  // See the comments in CongestionEventSample for the return value.
  // |max_bandwidth| is the windowed maximum observed bandwidth.
  // |est_bandwidth_upper_bound| is an upper bound of estimated bandwidth used
  // to calculate extra_acked.
  virtual CongestionEventSample OnCongestionEvent(
      QuicTime ack_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      QuicBandwidth max_bandwidth,
      QuicBandwidth est_bandwidth_upper_bound,
      QuicRoundTripCount round_trip_count) = 0;

  // Informs the sampler that the connection is currently app-limited, causing
  // the sampler to enter the app-limited phase.  The phase will expire by
  // itself.
  virtual void OnAppLimited() = 0;

  // Remove all the packets lower than the specified packet number.
  virtual void RemoveObsoletePackets(QuicPacketNumber least_unacked) = 0;

  // Total number of bytes sent/acked/lost/neutered in the connection.
  virtual QuicByteCount total_bytes_sent() const = 0;
  virtual QuicByteCount total_bytes_acked() const = 0;
  virtual QuicByteCount total_bytes_lost() const = 0;
  virtual QuicByteCount total_bytes_neutered() const = 0;

  // Application-limited information exported for debugging.
  virtual bool is_app_limited() const = 0;

  virtual QuicPacketNumber end_of_app_limited_phase() const = 0;
};

// BandwidthSampler keeps track of sent and acknowledged packets and outputs a
// bandwidth sample for every packet acknowledged. The samples are taken for
// individual packets, and are not filtered; the consumer has to filter the
// bandwidth samples itself. In certain cases, the sampler will locally severely
// underestimate the bandwidth, hence a maximum filter with a size of at least
// one RTT is recommended.
//
// This class bases its samples on the slope of two curves: the number of bytes
// sent over time, and the number of bytes acknowledged as received over time.
// It produces a sample of both slopes for every packet that gets acknowledged,
// based on a slope between two points on each of the corresponding curves. Note
// that due to the packet loss, the number of bytes on each curve might get
// further and further away from each other, meaning that it is not feasible to
// compare byte values coming from different curves with each other.
//
// The obvious points for measuring slope sample are the ones corresponding to
// the packet that was just acknowledged. Let us denote them as S_1 (point at
// which the current packet was sent) and A_1 (point at which the current packet
// was acknowledged). However, taking a slope requires two points on each line,
// so estimating bandwidth requires picking a packet in the past with respect to
// which the slope is measured.
//
// For that purpose, BandwidthSampler always keeps track of the most recently
// acknowledged packet, and records it together with every outgoing packet.
// When a packet gets acknowledged (A_1), it has not only information about when
// it itself was sent (S_1), but also the information about a previously
// acknowledged packet before it was sent (S_0 and A_0).
//
// Based on that data, send and ack rate are estimated as:
//   send_rate = (bytes(S_1) - bytes(S_0)) / (time(S_1) - time(S_0))
//   ack_rate = (bytes(A_1) - bytes(A_0)) / (time(A_1) - time(A_0))
//
// Here, the ack rate is intuitively the rate we want to treat as bandwidth.
// However, in certain cases (e.g. ack compression) the ack rate at a point may
// end up higher than the rate at which the data was originally sent, which is
// not indicative of the real bandwidth. Hence, we use the send rate as an upper
// bound, and the sample value is
//   rate_sample = min(send_rate, ack_rate)
//
// An important edge case handled by the sampler is tracking the app-limited
// samples. There are multiple meaning of "app-limited" used interchangeably,
// hence it is important to understand and to be able to distinguish between
// them.
//
// Meaning 1: connection state. The connection is said to be app-limited when
// there is no outstanding data to send. This means that certain bandwidth
// samples in the future would not be an accurate indication of the link
// capacity, and it is important to inform consumer about that. Whenever
// connection becomes app-limited, the sampler is notified via OnAppLimited()
// method.
//
// Meaning 2: a phase in the bandwidth sampler. As soon as the bandwidth
// sampler becomes notified about the connection being app-limited, it enters
// app-limited phase. In that phase, all *sent* packets are marked as
// app-limited. Note that the connection itself does not have to be
// app-limited during the app-limited phase, and in fact it will not be
// (otherwise how would it send packets?). The boolean flag below indicates
// whether the sampler is in that phase.
//
// Meaning 3: a flag on the sent packet and on the sample. If a sent packet is
// sent during the app-limited phase, the resulting sample related to the
// packet will be marked as app-limited.
//
// With the terminology issue out of the way, let us consider the question of
// what kind of situation it addresses.
//
// Consider a scenario where we first send packets 1 to 20 at a regular
// bandwidth, and then immediately run out of data. After a few seconds, we send
// packets 21 to 60, and only receive ack for 21 between sending packets 40 and
// 41. In this case, when we sample bandwidth for packets 21 to 40, the S_0/A_0
// we use to compute the slope is going to be packet 20, a few seconds apart
// from the current packet, hence the resulting estimate would be extremely low
// and not indicative of anything. Only at packet 41 the S_0/A_0 will become 21,
// meaning that the bandwidth sample would exclude the quiescence.
//
// Based on the analysis of that scenario, we implement the following rule: once
// OnAppLimited() is called, all sent packets will produce app-limited samples
// up until an ack for a packet that was sent after OnAppLimited() was called.
// Note that while the scenario above is not the only scenario when the
// connection is app-limited, the approach works in other cases too.
class QUIC_EXPORT_PRIVATE BandwidthSampler : public BandwidthSamplerInterface {
 public:
  BandwidthSampler(const QuicUnackedPacketMap* unacked_packet_map,
                   QuicRoundTripCount max_height_tracker_window_length);

  // Copy states from |other|. This is useful when changing send algorithms in
  // the middle of a connection.
  BandwidthSampler(const BandwidthSampler& other);
  ~BandwidthSampler() override;

  void OnPacketSent(QuicTime sent_time,
                    QuicPacketNumber packet_number,
                    QuicByteCount bytes,
                    QuicByteCount bytes_in_flight,
                    HasRetransmittableData has_retransmittable_data) override;
  void OnPacketNeutered(QuicPacketNumber packet_number) override;

  CongestionEventSample OnCongestionEvent(
      QuicTime ack_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      QuicBandwidth max_bandwidth,
      QuicBandwidth est_bandwidth_upper_bound,
      QuicRoundTripCount round_trip_count) override;
  QuicByteCount OnAckEventEnd(QuicBandwidth bandwidth_estimate,
                              QuicRoundTripCount round_trip_count);

  void OnAppLimited() override;

  void RemoveObsoletePackets(QuicPacketNumber least_unacked) override;

  QuicByteCount total_bytes_sent() const override;
  QuicByteCount total_bytes_acked() const override;
  QuicByteCount total_bytes_lost() const override;
  QuicByteCount total_bytes_neutered() const override;

  bool is_app_limited() const override;

  QuicPacketNumber end_of_app_limited_phase() const override;

  QuicByteCount max_ack_height() const { return max_ack_height_tracker_.Get(); }

  uint64_t num_ack_aggregation_epochs() const {
    return max_ack_height_tracker_.num_ack_aggregation_epochs();
  }

  void SetMaxAckHeightTrackerWindowLength(QuicRoundTripCount length) {
    max_ack_height_tracker_.SetFilterWindowLength(length);
  }

  void ResetMaxAckHeightTracker(QuicByteCount new_height,
                                QuicRoundTripCount new_time) {
    max_ack_height_tracker_.Reset(new_height, new_time);
  }

  // AckPoint represents a point on the ack line.
  struct QUIC_NO_EXPORT AckPoint {
    QuicTime ack_time = QuicTime::Zero();
    QuicByteCount total_bytes_acked = 0;

    friend QUIC_NO_EXPORT std::ostream& operator<<(std::ostream& os,
                                                   const AckPoint& ack_point) {
      return os << ack_point.ack_time << ":" << ack_point.total_bytes_acked;
    }
  };

  // RecentAckPoints maintains the most recent 2 ack points at distinct times.
  class QUIC_NO_EXPORT RecentAckPoints {
   public:
    void Update(QuicTime ack_time, QuicByteCount total_bytes_acked) {
      DCHECK_GE(total_bytes_acked, ack_points_[1].total_bytes_acked);

      if (ack_time < ack_points_[1].ack_time) {
        // This can only happen when time goes backwards, we use the smaller
        // timestamp for the most recent ack point in that case.
        // TODO(wub): Add a QUIC_BUG if ack time stops going backwards.
        ack_points_[1].ack_time = ack_time;
      } else if (ack_time > ack_points_[1].ack_time) {
        ack_points_[0] = ack_points_[1];
        ack_points_[1].ack_time = ack_time;
      }

      ack_points_[1].total_bytes_acked = total_bytes_acked;
    }

    void Clear() { ack_points_[0] = ack_points_[1] = AckPoint(); }

    const AckPoint& MostRecentPoint() const { return ack_points_[1]; }

    const AckPoint& LessRecentPoint() const {
      if (ack_points_[0].total_bytes_acked != 0) {
        return ack_points_[0];
      }

      return ack_points_[1];
    }

   private:
    AckPoint ack_points_[2];
  };

  void EnableOverestimateAvoidance();
  bool IsOverestimateAvoidanceEnabled() const {
    return overestimate_avoidance_;
  }

 private:
  friend class test::BandwidthSamplerPeer;

  // ConnectionStateOnSentPacket represents the information about a sent packet
  // and the state of the connection at the moment the packet was sent,
  // specifically the information about the most recently acknowledged packet at
  // that moment.
  struct QUIC_EXPORT_PRIVATE ConnectionStateOnSentPacket {
    // Time at which the packet is sent.
    QuicTime sent_time;

    // Size of the packet.
    QuicByteCount size;

    // The value of |total_bytes_sent_at_last_acked_packet_| at the time the
    // packet was sent.
    QuicByteCount total_bytes_sent_at_last_acked_packet;

    // The value of |last_acked_packet_sent_time_| at the time the packet was
    // sent.
    QuicTime last_acked_packet_sent_time;

    // The value of |last_acked_packet_ack_time_| at the time the packet was
    // sent.
    QuicTime last_acked_packet_ack_time;

    // Send time states that are returned to the congestion controller when the
    // packet is acked or lost.
    SendTimeState send_time_state;

    // Snapshot constructor. Records the current state of the bandwidth
    // sampler.
    // |bytes_in_flight| is the bytes in flight right after the packet is sent.
    ConnectionStateOnSentPacket(QuicTime sent_time,
                                QuicByteCount size,
                                QuicByteCount bytes_in_flight,
                                const BandwidthSampler& sampler)
        : sent_time(sent_time),
          size(size),
          total_bytes_sent_at_last_acked_packet(
              sampler.total_bytes_sent_at_last_acked_packet_),
          last_acked_packet_sent_time(sampler.last_acked_packet_sent_time_),
          last_acked_packet_ack_time(sampler.last_acked_packet_ack_time_),
          send_time_state(sampler.is_app_limited_,
                          sampler.total_bytes_sent_,
                          sampler.total_bytes_acked_,
                          sampler.total_bytes_lost_,
                          bytes_in_flight) {}

    // Default constructor.  Required to put this structure into
    // PacketNumberIndexedQueue.
    ConnectionStateOnSentPacket()
        : sent_time(QuicTime::Zero()),
          size(0),
          total_bytes_sent_at_last_acked_packet(0),
          last_acked_packet_sent_time(QuicTime::Zero()),
          last_acked_packet_ack_time(QuicTime::Zero()) {}

    friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
        std::ostream& os,
        const ConnectionStateOnSentPacket& p) {
      os << "{sent_time:" << p.sent_time << ", size:" << p.size
         << ", total_bytes_sent_at_last_acked_packet:"
         << p.total_bytes_sent_at_last_acked_packet
         << ", last_acked_packet_sent_time:" << p.last_acked_packet_sent_time
         << ", last_acked_packet_ack_time:" << p.last_acked_packet_ack_time
         << ", send_time_state:" << p.send_time_state << "}";
      return os;
    }
  };

  BandwidthSample OnPacketAcknowledged(QuicTime ack_time,
                                       QuicPacketNumber packet_number);

  SendTimeState OnPacketLost(QuicPacketNumber packet_number,
                             QuicPacketLength bytes_lost);

  // Copy a subset of the (private) ConnectionStateOnSentPacket to the (public)
  // SendTimeState. Always set send_time_state->is_valid to true.
  void SentPacketToSendTimeState(const ConnectionStateOnSentPacket& sent_packet,
                                 SendTimeState* send_time_state) const;

  // Choose the best a0 from |a0_candidates_| to calculate the ack rate.
  // |total_bytes_acked| is the total bytes acked when the packet being acked is
  // sent. The best a0 is chosen as follows:
  // - If there's only one candidate, use it.
  // - If there are multiple candidates, let a[n] be the nth candidate, and
  //   a[n-1].total_bytes_acked <= |total_bytes_acked| < a[n].total_bytes_acked,
  //   use a[n-1].
  // - If all candidates's total_bytes_acked is > |total_bytes_acked|, use a[0].
  //   This may happen when acks are received out of order, and ack[n] caused
  //   some candidates of ack[n-x] to be removed.
  // - If all candidates's total_bytes_acked is <= |total_bytes_acked|, use
  //   a[a.size()-1].
  bool ChooseA0Point(QuicByteCount total_bytes_acked, AckPoint* a0);

  // The total number of congestion controlled bytes sent during the connection.
  QuicByteCount total_bytes_sent_;

  // The total number of congestion controlled bytes which were acknowledged.
  QuicByteCount total_bytes_acked_;

  // The total number of congestion controlled bytes which were lost.
  QuicByteCount total_bytes_lost_;

  // The total number of congestion controlled bytes which have been neutered.
  QuicByteCount total_bytes_neutered_;

  // The value of |total_bytes_sent_| at the time the last acknowledged packet
  // was sent. Valid only when |last_acked_packet_sent_time_| is valid.
  QuicByteCount total_bytes_sent_at_last_acked_packet_;

  // The time at which the last acknowledged packet was sent. Set to
  // QuicTime::Zero() if no valid timestamp is available.
  QuicTime last_acked_packet_sent_time_;

  // The time at which the most recent packet was acknowledged.
  QuicTime last_acked_packet_ack_time_;

  // The most recently sent packet.
  QuicPacketNumber last_sent_packet_;

  // Indicates whether the bandwidth sampler is started in app-limited phase.
  const bool started_as_app_limited_ =
      GetQuicReloadableFlag(quic_bw_sampler_app_limited_starting_value);

  // Indicates whether the bandwidth sampler is currently in an app-limited
  // phase.
  bool is_app_limited_;

  // The packet that will be acknowledged after this one will cause the sampler
  // to exit the app-limited phase.
  QuicPacketNumber end_of_app_limited_phase_;

  // Record of the connection state at the point where each packet in flight was
  // sent, indexed by the packet number.
  PacketNumberIndexedQueue<ConnectionStateOnSentPacket> connection_state_map_;

  RecentAckPoints recent_ack_points_;
  QuicCircularDeque<AckPoint> a0_candidates_;

  // Maximum number of tracked packets.
  const QuicPacketCount max_tracked_packets_;

  // The main unacked packet map.  Used for outputting extra debugging details.
  // May be null.
  // TODO(vasilvv): remove this once it's no longer useful for debugging.
  const QuicUnackedPacketMap* unacked_packet_map_;

  // Handles the actual bandwidth calculations, whereas the outer method handles
  // retrieving and removing |sent_packet|.
  BandwidthSample OnPacketAcknowledgedInner(
      QuicTime ack_time,
      QuicPacketNumber packet_number,
      const ConnectionStateOnSentPacket& sent_packet);

  MaxAckHeightTracker max_ack_height_tracker_;
  QuicByteCount total_bytes_acked_after_last_ack_event_;

  // True if --quic_avoid_overestimate_bandwidth_with_aggregation=true and
  // connection option 'BSAO' is set.
  bool overestimate_avoidance_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BANDWIDTH_SAMPLER_H_
