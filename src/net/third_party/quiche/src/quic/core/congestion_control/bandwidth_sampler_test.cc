// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"
#include <cstdint>
#include <set>

#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {
namespace test {

class BandwidthSamplerPeer {
 public:
  static size_t GetNumberOfTrackedPackets(const BandwidthSampler& sampler) {
    return sampler.connection_state_map_.number_of_present_entries();
  }

  static QuicByteCount GetPacketSize(const BandwidthSampler& sampler,
                                     QuicPacketNumber packet_number) {
    return sampler.connection_state_map_.GetEntry(packet_number)->size;
  }
};

const QuicByteCount kRegularPacketSize = 1280;
// Enforce divisibility for some of the tests.
static_assert((kRegularPacketSize & 31) == 0,
              "kRegularPacketSize has to be five times divisible by 2");

// A test fixture with utility methods for BandwidthSampler tests.
class BandwidthSamplerTest : public QuicTest {
 protected:
  BandwidthSamplerTest()
      : sampler_(nullptr, /*max_height_tracker_window_length=*/0),
        sampler_app_limited_at_start_(sampler_.is_app_limited()),
        bytes_in_flight_(0),
        max_bandwidth_(QuicBandwidth::Zero()),
        est_bandwidth_upper_bound_(QuicBandwidth::Infinite()),
        round_trip_count_(0) {
    // Ensure that the clock does not start at zero.
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
    if (GetQuicReloadableFlag(
            quic_avoid_overestimate_bandwidth_with_aggregation)) {
      sampler_.EnableOverestimateAvoidance();
    }
  }

  MockClock clock_;
  BandwidthSampler sampler_;
  bool sampler_app_limited_at_start_;
  QuicByteCount bytes_in_flight_;
  QuicBandwidth max_bandwidth_;  // Max observed bandwidth from acks.
  QuicBandwidth est_bandwidth_upper_bound_;
  QuicRoundTripCount round_trip_count_;  // Needed to calculate extra_acked.

  QuicByteCount PacketsToBytes(QuicPacketCount packet_count) {
    return packet_count * kRegularPacketSize;
  }

  void SendPacketInner(uint64_t packet_number,
                       QuicByteCount bytes,
                       HasRetransmittableData has_retransmittable_data) {
    sampler_.OnPacketSent(clock_.Now(), QuicPacketNumber(packet_number), bytes,
                          bytes_in_flight_, has_retransmittable_data);
    if (has_retransmittable_data == HAS_RETRANSMITTABLE_DATA) {
      bytes_in_flight_ += bytes;
    }
  }

  void SendPacket(uint64_t packet_number) {
    SendPacketInner(packet_number, kRegularPacketSize,
                    HAS_RETRANSMITTABLE_DATA);
  }

  BandwidthSample AckPacketInner(uint64_t packet_number) {
    QuicByteCount size = BandwidthSamplerPeer::GetPacketSize(
        sampler_, QuicPacketNumber(packet_number));
    bytes_in_flight_ -= size;
    BandwidthSampler::CongestionEventSample sample = sampler_.OnCongestionEvent(
        clock_.Now(), {MakeAckedPacket(packet_number)}, {}, max_bandwidth_,
        est_bandwidth_upper_bound_, round_trip_count_);
    max_bandwidth_ = std::max(max_bandwidth_, sample.sample_max_bandwidth);
    BandwidthSample bandwidth_sample;
    bandwidth_sample.bandwidth = sample.sample_max_bandwidth;
    bandwidth_sample.rtt = sample.sample_rtt;
    bandwidth_sample.state_at_send = sample.last_packet_send_state;
    EXPECT_TRUE(bandwidth_sample.state_at_send.is_valid);
    return bandwidth_sample;
  }

  AckedPacket MakeAckedPacket(uint64_t packet_number) const {
    QuicByteCount size = BandwidthSamplerPeer::GetPacketSize(
        sampler_, QuicPacketNumber(packet_number));
    return AckedPacket(QuicPacketNumber(packet_number), size, clock_.Now());
  }

  LostPacket MakeLostPacket(uint64_t packet_number) const {
    return LostPacket(QuicPacketNumber(packet_number),
                      BandwidthSamplerPeer::GetPacketSize(
                          sampler_, QuicPacketNumber(packet_number)));
  }

  // Acknowledge receipt of a packet and expect it to be not app-limited.
  QuicBandwidth AckPacket(uint64_t packet_number) {
    BandwidthSample sample = AckPacketInner(packet_number);
    return sample.bandwidth;
  }

  BandwidthSampler::CongestionEventSample OnCongestionEvent(
      std::set<uint64_t> acked_packet_numbers,
      std::set<uint64_t> lost_packet_numbers) {
    AckedPacketVector acked_packets;
    for (auto it = acked_packet_numbers.begin();
         it != acked_packet_numbers.end(); ++it) {
      acked_packets.push_back(MakeAckedPacket(*it));
      bytes_in_flight_ -= acked_packets.back().bytes_acked;
    }

    LostPacketVector lost_packets;
    for (auto it = lost_packet_numbers.begin(); it != lost_packet_numbers.end();
         ++it) {
      lost_packets.push_back(MakeLostPacket(*it));
      bytes_in_flight_ -= lost_packets.back().bytes_lost;
    }

    BandwidthSampler::CongestionEventSample sample = sampler_.OnCongestionEvent(
        clock_.Now(), acked_packets, lost_packets, max_bandwidth_,
        est_bandwidth_upper_bound_, round_trip_count_);
    max_bandwidth_ = std::max(max_bandwidth_, sample.sample_max_bandwidth);
    return sample;
  }

  SendTimeState LosePacket(uint64_t packet_number) {
    QuicByteCount size = BandwidthSamplerPeer::GetPacketSize(
        sampler_, QuicPacketNumber(packet_number));
    bytes_in_flight_ -= size;
    LostPacket lost_packet(QuicPacketNumber(packet_number), size);
    BandwidthSampler::CongestionEventSample sample = sampler_.OnCongestionEvent(
        clock_.Now(), {}, {lost_packet}, max_bandwidth_,
        est_bandwidth_upper_bound_, round_trip_count_);
    EXPECT_TRUE(sample.last_packet_send_state.is_valid);
    EXPECT_EQ(sample.sample_max_bandwidth, QuicBandwidth::Zero());
    EXPECT_EQ(sample.sample_rtt, QuicTime::Delta::Infinite());
    return sample.last_packet_send_state;
  }

  // Sends one packet and acks it.  Then, send 20 packets.  Finally, send
  // another 20 packets while acknowledging previous 20.
  void Send40PacketsAndAckFirst20(QuicTime::Delta time_between_packets) {
    // Send 20 packets at a constant inter-packet time.
    for (int i = 1; i <= 20; i++) {
      SendPacket(i);
      clock_.AdvanceTime(time_between_packets);
    }

    // Ack packets 1 to 20, while sending new packets at the same rate as
    // before.
    for (int i = 1; i <= 20; i++) {
      AckPacket(i);
      SendPacket(i + 20);
      clock_.AdvanceTime(time_between_packets);
    }
  }
};

// Test the sampler in a simple stop-and-wait sender setting.
TEST_F(BandwidthSamplerTest, SendAndWait) {
  QuicTime::Delta time_between_packets = QuicTime::Delta::FromMilliseconds(10);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromBytesPerSecond(kRegularPacketSize * 100);

  // Send packets at the constant bandwidth.
  for (int i = 1; i < 20; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
    QuicBandwidth current_sample = AckPacket(i);
    EXPECT_EQ(expected_bandwidth, current_sample);
  }

  // Send packets at the exponentially decreasing bandwidth.
  for (int i = 20; i < 25; i++) {
    time_between_packets = time_between_packets * 2;
    expected_bandwidth = expected_bandwidth * 0.5;

    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
    QuicBandwidth current_sample = AckPacket(i);
    EXPECT_EQ(expected_bandwidth, current_sample);
  }
  sampler_.RemoveObsoletePackets(QuicPacketNumber(25));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

TEST_F(BandwidthSamplerTest, SendTimeState) {
  QuicTime::Delta time_between_packets = QuicTime::Delta::FromMilliseconds(10);

  // Send packets 1-5.
  for (int i = 1; i <= 5; i++) {
    SendPacket(i);
    EXPECT_EQ(PacketsToBytes(i), sampler_.total_bytes_sent());
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack packet 1.
  SendTimeState send_time_state = AckPacketInner(1).state_at_send;
  EXPECT_EQ(PacketsToBytes(1), send_time_state.total_bytes_sent);
  EXPECT_EQ(0u, send_time_state.total_bytes_acked);
  EXPECT_EQ(0u, send_time_state.total_bytes_lost);
  EXPECT_EQ(PacketsToBytes(1), sampler_.total_bytes_acked());

  // Lose packet 2.
  send_time_state = LosePacket(2);
  EXPECT_EQ(PacketsToBytes(2), send_time_state.total_bytes_sent);
  EXPECT_EQ(0u, send_time_state.total_bytes_acked);
  EXPECT_EQ(0u, send_time_state.total_bytes_lost);
  EXPECT_EQ(PacketsToBytes(1), sampler_.total_bytes_lost());

  // Lose packet 3.
  send_time_state = LosePacket(3);
  EXPECT_EQ(PacketsToBytes(3), send_time_state.total_bytes_sent);
  EXPECT_EQ(0u, send_time_state.total_bytes_acked);
  EXPECT_EQ(0u, send_time_state.total_bytes_lost);
  EXPECT_EQ(PacketsToBytes(2), sampler_.total_bytes_lost());

  // Send packets 6-10.
  for (int i = 6; i <= 10; i++) {
    SendPacket(i);
    EXPECT_EQ(PacketsToBytes(i), sampler_.total_bytes_sent());
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack all inflight packets.
  QuicPacketCount acked_packet_count = 1;
  EXPECT_EQ(PacketsToBytes(acked_packet_count), sampler_.total_bytes_acked());
  for (int i = 4; i <= 10; i++) {
    send_time_state = AckPacketInner(i).state_at_send;
    ++acked_packet_count;
    EXPECT_EQ(PacketsToBytes(acked_packet_count), sampler_.total_bytes_acked());
    EXPECT_EQ(PacketsToBytes(i), send_time_state.total_bytes_sent);
    if (i <= 5) {
      EXPECT_EQ(0u, send_time_state.total_bytes_acked);
      EXPECT_EQ(0u, send_time_state.total_bytes_lost);
    } else {
      EXPECT_EQ(PacketsToBytes(1), send_time_state.total_bytes_acked);
      EXPECT_EQ(PacketsToBytes(2), send_time_state.total_bytes_lost);
    }

    // This equation works because there is no neutered bytes.
    EXPECT_EQ(send_time_state.total_bytes_sent -
                  send_time_state.total_bytes_acked -
                  send_time_state.total_bytes_lost,
              send_time_state.bytes_in_flight);

    clock_.AdvanceTime(time_between_packets);
  }
}

// Test the sampler during regular windowed sender scenario with fixed
// CWND of 20.
TEST_F(BandwidthSamplerTest, SendPaced) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromKBytesPerSecond(kRegularPacketSize);

  Send40PacketsAndAckFirst20(time_between_packets);

  // Ack the packets 21 to 40, arriving at the correct bandwidth.
  QuicBandwidth last_bandwidth = QuicBandwidth::Zero();
  for (int i = 21; i <= 40; i++) {
    last_bandwidth = AckPacket(i);
    EXPECT_EQ(expected_bandwidth, last_bandwidth) << "i is " << i;
    clock_.AdvanceTime(time_between_packets);
  }
  sampler_.RemoveObsoletePackets(QuicPacketNumber(41));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

// Test the sampler in a scenario where 50% of packets is consistently lost.
TEST_F(BandwidthSamplerTest, SendWithLosses) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromKBytesPerSecond(kRegularPacketSize) * 0.5;

  // Send 20 packets, each 1 ms apart.
  for (int i = 1; i <= 20; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack packets 1 to 20, losing every even-numbered packet, while sending new
  // packets at the same rate as before.
  for (int i = 1; i <= 20; i++) {
    if (i % 2 == 0) {
      AckPacket(i);
    } else {
      LosePacket(i);
    }
    SendPacket(i + 20);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack the packets 21 to 40 with the same loss pattern.
  QuicBandwidth last_bandwidth = QuicBandwidth::Zero();
  for (int i = 21; i <= 40; i++) {
    if (i % 2 == 0) {
      last_bandwidth = AckPacket(i);
      EXPECT_EQ(expected_bandwidth, last_bandwidth);
    } else {
      LosePacket(i);
    }
    clock_.AdvanceTime(time_between_packets);
  }
  sampler_.RemoveObsoletePackets(QuicPacketNumber(41));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

// Test the sampler in a scenario where the 50% of packets are not
// congestion controlled (specifically, non-retransmittable data is not
// congestion controlled).  Should be functionally consistent in behavior with
// the SendWithLosses test.
TEST_F(BandwidthSamplerTest, NotCongestionControlled) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromKBytesPerSecond(kRegularPacketSize) * 0.5;

  // Send 20 packets, each 1 ms apart. Every even packet is not congestion
  // controlled.
  for (int i = 1; i <= 20; i++) {
    SendPacketInner(
        i, kRegularPacketSize,
        i % 2 == 0 ? HAS_RETRANSMITTABLE_DATA : NO_RETRANSMITTABLE_DATA);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ensure only congestion controlled packets are tracked.
  EXPECT_EQ(10u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));

  // Ack packets 2 to 21, ignoring every even-numbered packet, while sending new
  // packets at the same rate as before.
  for (int i = 1; i <= 20; i++) {
    if (i % 2 == 0) {
      AckPacket(i);
    }
    SendPacketInner(
        i + 20, kRegularPacketSize,
        i % 2 == 0 ? HAS_RETRANSMITTABLE_DATA : NO_RETRANSMITTABLE_DATA);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack the packets 22 to 41 with the same congestion controlled pattern.
  QuicBandwidth last_bandwidth = QuicBandwidth::Zero();
  for (int i = 21; i <= 40; i++) {
    if (i % 2 == 0) {
      last_bandwidth = AckPacket(i);
      EXPECT_EQ(expected_bandwidth, last_bandwidth);
    }
    clock_.AdvanceTime(time_between_packets);
  }
  sampler_.RemoveObsoletePackets(QuicPacketNumber(41));

  // Since only congestion controlled packets are entered into the map, it has
  // to be empty at this point.
  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

// Simulate a situation where ACKs arrive in burst and earlier than usual, thus
// producing an ACK rate which is higher than the original send rate.
TEST_F(BandwidthSamplerTest, CompressedAck) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromKBytesPerSecond(kRegularPacketSize);

  Send40PacketsAndAckFirst20(time_between_packets);

  // Simulate an RTT somewhat lower than the one for 1-to-21 transmission.
  clock_.AdvanceTime(time_between_packets * 15);

  // Ack the packets 21 to 40 almost immediately at once.
  QuicBandwidth last_bandwidth = QuicBandwidth::Zero();
  QuicTime::Delta ridiculously_small_time_delta =
      QuicTime::Delta::FromMicroseconds(20);
  for (int i = 21; i <= 40; i++) {
    last_bandwidth = AckPacket(i);
    clock_.AdvanceTime(ridiculously_small_time_delta);
  }
  EXPECT_EQ(expected_bandwidth, last_bandwidth);

  sampler_.RemoveObsoletePackets(QuicPacketNumber(41));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

// Tests receiving ACK packets in the reverse order.
TEST_F(BandwidthSamplerTest, ReorderedAck) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromKBytesPerSecond(kRegularPacketSize);

  Send40PacketsAndAckFirst20(time_between_packets);

  // Ack the packets 21 to 40 in the reverse order, while sending packets 41 to
  // 60.
  QuicBandwidth last_bandwidth = QuicBandwidth::Zero();
  for (int i = 0; i < 20; i++) {
    last_bandwidth = AckPacket(40 - i);
    EXPECT_EQ(expected_bandwidth, last_bandwidth);
    SendPacket(41 + i);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack the packets 41 to 60, now in the regular order.
  for (int i = 41; i <= 60; i++) {
    last_bandwidth = AckPacket(i);
    EXPECT_EQ(expected_bandwidth, last_bandwidth);
    clock_.AdvanceTime(time_between_packets);
  }
  sampler_.RemoveObsoletePackets(QuicPacketNumber(61));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

// Test the app-limited logic.
TEST_F(BandwidthSamplerTest, AppLimited) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  QuicBandwidth expected_bandwidth =
      QuicBandwidth::FromKBytesPerSecond(kRegularPacketSize);

  // Send 20 packets at a constant inter-packet time.
  for (int i = 1; i <= 20; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack packets 1 to 20, while sending new packets at the same rate as
  // before.
  for (int i = 1; i <= 20; i++) {
    BandwidthSample sample = AckPacketInner(i);
    EXPECT_EQ(sample.state_at_send.is_app_limited,
              sampler_app_limited_at_start_);
    SendPacket(i + 20);
    clock_.AdvanceTime(time_between_packets);
  }

  // We are now app-limited. Ack 21 to 40 as usual, but do not send anything for
  // now.
  sampler_.OnAppLimited();
  for (int i = 21; i <= 40; i++) {
    BandwidthSample sample = AckPacketInner(i);
    EXPECT_FALSE(sample.state_at_send.is_app_limited);
    EXPECT_EQ(expected_bandwidth, sample.bandwidth);
    clock_.AdvanceTime(time_between_packets);
  }

  // Enter quiescence.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));

  // Send packets 41 to 60, all of which would be marked as app-limited.
  for (int i = 41; i <= 60; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
  }

  // Ack packets 41 to 60, while sending packets 61 to 80.  41 to 60 should be
  // app-limited and underestimate the bandwidth due to that.
  for (int i = 41; i <= 60; i++) {
    BandwidthSample sample = AckPacketInner(i);
    EXPECT_TRUE(sample.state_at_send.is_app_limited);
    EXPECT_LT(sample.bandwidth, 0.7f * expected_bandwidth);

    SendPacket(i + 20);
    clock_.AdvanceTime(time_between_packets);
  }

  // Run out of packets, and then ack packet 61 to 80, all of which should have
  // correct non-app-limited samples.
  for (int i = 61; i <= 80; i++) {
    BandwidthSample sample = AckPacketInner(i);
    EXPECT_FALSE(sample.state_at_send.is_app_limited);
    EXPECT_EQ(sample.bandwidth, expected_bandwidth);
    clock_.AdvanceTime(time_between_packets);
  }
  sampler_.RemoveObsoletePackets(QuicPacketNumber(81));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  EXPECT_EQ(0u, bytes_in_flight_);
}

// Test the samples taken at the first flight of packets sent.
TEST_F(BandwidthSamplerTest, FirstRoundTrip) {
  const QuicTime::Delta time_between_packets =
      QuicTime::Delta::FromMilliseconds(1);
  const QuicTime::Delta rtt = QuicTime::Delta::FromMilliseconds(800);
  const int num_packets = 10;
  const QuicByteCount num_bytes = kRegularPacketSize * num_packets;
  const QuicBandwidth real_bandwidth =
      QuicBandwidth::FromBytesAndTimeDelta(num_bytes, rtt);

  for (int i = 1; i <= 10; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
  }

  clock_.AdvanceTime(rtt - num_packets * time_between_packets);

  QuicBandwidth last_sample = QuicBandwidth::Zero();
  for (int i = 1; i <= 10; i++) {
    QuicBandwidth sample = AckPacket(i);
    EXPECT_GT(sample, last_sample);
    last_sample = sample;
    clock_.AdvanceTime(time_between_packets);
  }

  // The final measured sample for the first flight of sample is expected to be
  // smaller than the real bandwidth, yet it should not lose more than 10%. The
  // specific value of the error depends on the difference between the RTT and
  // the time it takes to exhaust the congestion window (i.e. in the limit when
  // all packets are sent simultaneously, last sample would indicate the real
  // bandwidth).
  EXPECT_LT(last_sample, real_bandwidth);
  EXPECT_GT(last_sample, 0.9f * real_bandwidth);
}

// Test sampler's ability to remove obsolete packets.
TEST_F(BandwidthSamplerTest, RemoveObsoletePackets) {
  SendPacket(1);
  SendPacket(2);
  SendPacket(3);
  SendPacket(4);
  SendPacket(5);

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(100));

  EXPECT_EQ(5u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  sampler_.RemoveObsoletePackets(QuicPacketNumber(4));
  EXPECT_EQ(2u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  LosePacket(4);
  sampler_.RemoveObsoletePackets(QuicPacketNumber(5));

  EXPECT_EQ(1u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
  AckPacket(5);

  sampler_.RemoveObsoletePackets(QuicPacketNumber(6));

  EXPECT_EQ(0u, BandwidthSamplerPeer::GetNumberOfTrackedPackets(sampler_));
}

TEST_F(BandwidthSamplerTest, NeuterPacket) {
  SendPacket(1);
  EXPECT_EQ(0u, sampler_.total_bytes_neutered());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  sampler_.OnPacketNeutered(QuicPacketNumber(1));
  EXPECT_LT(0u, sampler_.total_bytes_neutered());
  EXPECT_EQ(0u, sampler_.total_bytes_acked());

  // If packet 1 is acked it should not produce a bandwidth sample.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  BandwidthSampler::CongestionEventSample sample = sampler_.OnCongestionEvent(
      clock_.Now(),
      {AckedPacket(QuicPacketNumber(1), kRegularPacketSize, clock_.Now())}, {},
      max_bandwidth_, est_bandwidth_upper_bound_, round_trip_count_);
  EXPECT_EQ(0u, sampler_.total_bytes_acked());
  EXPECT_EQ(QuicBandwidth::Zero(), sample.sample_max_bandwidth);
  EXPECT_FALSE(sample.sample_is_app_limited);
  EXPECT_EQ(QuicTime::Delta::Infinite(), sample.sample_rtt);
  EXPECT_EQ(0u, sample.sample_max_inflight);
  EXPECT_EQ(0u, sample.extra_acked);
}

TEST_F(BandwidthSamplerTest, CongestionEventSampleDefaultValues) {
  // Make sure a default constructed CongestionEventSample has the correct
  // initial values for BandwidthSampler::OnCongestionEvent() to work.
  BandwidthSampler::CongestionEventSample sample;

  EXPECT_EQ(QuicBandwidth::Zero(), sample.sample_max_bandwidth);
  EXPECT_FALSE(sample.sample_is_app_limited);
  EXPECT_EQ(QuicTime::Delta::Infinite(), sample.sample_rtt);
  EXPECT_EQ(0u, sample.sample_max_inflight);
  EXPECT_EQ(0u, sample.extra_acked);
}

// 1) Send 2 packets, 2) Ack both in 1 event, 3) Repeat.
TEST_F(BandwidthSamplerTest, TwoAckedPacketsPerEvent) {
  QuicTime::Delta time_between_packets = QuicTime::Delta::FromMilliseconds(10);
  QuicBandwidth sending_rate = QuicBandwidth::FromBytesAndTimeDelta(
      kRegularPacketSize, time_between_packets);

  for (uint64_t i = 1; i < 21; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
    if (i % 2 != 0) {
      continue;
    }

    BandwidthSampler::CongestionEventSample sample =
        OnCongestionEvent({i - 1, i}, {});
    EXPECT_EQ(sending_rate, sample.sample_max_bandwidth);
    EXPECT_EQ(time_between_packets, sample.sample_rtt);
    EXPECT_EQ(2 * kRegularPacketSize, sample.sample_max_inflight);
    EXPECT_TRUE(sample.last_packet_send_state.is_valid);
    EXPECT_EQ(2 * kRegularPacketSize,
              sample.last_packet_send_state.bytes_in_flight);
    EXPECT_EQ(i * kRegularPacketSize,
              sample.last_packet_send_state.total_bytes_sent);
    EXPECT_EQ((i - 2) * kRegularPacketSize,
              sample.last_packet_send_state.total_bytes_acked);
    EXPECT_EQ(0u, sample.last_packet_send_state.total_bytes_lost);
    sampler_.RemoveObsoletePackets(QuicPacketNumber(i - 2));
  }
}

TEST_F(BandwidthSamplerTest, LoseEveryOtherPacket) {
  QuicTime::Delta time_between_packets = QuicTime::Delta::FromMilliseconds(10);
  QuicBandwidth sending_rate = QuicBandwidth::FromBytesAndTimeDelta(
      kRegularPacketSize, time_between_packets);

  for (uint64_t i = 1; i < 21; i++) {
    SendPacket(i);
    clock_.AdvanceTime(time_between_packets);
    if (i % 2 != 0) {
      continue;
    }

    // Ack packet i and lose i-1.
    BandwidthSampler::CongestionEventSample sample =
        OnCongestionEvent({i}, {i - 1});
    // Losing 50% packets means sending rate is twice the bandwidth.
    EXPECT_EQ(sending_rate, sample.sample_max_bandwidth * 2);
    EXPECT_EQ(time_between_packets, sample.sample_rtt);
    EXPECT_EQ(kRegularPacketSize, sample.sample_max_inflight);
    EXPECT_TRUE(sample.last_packet_send_state.is_valid);
    EXPECT_EQ(2 * kRegularPacketSize,
              sample.last_packet_send_state.bytes_in_flight);
    EXPECT_EQ(i * kRegularPacketSize,
              sample.last_packet_send_state.total_bytes_sent);
    EXPECT_EQ((i - 2) * kRegularPacketSize / 2,
              sample.last_packet_send_state.total_bytes_acked);
    EXPECT_EQ((i - 2) * kRegularPacketSize / 2,
              sample.last_packet_send_state.total_bytes_lost);
    sampler_.RemoveObsoletePackets(QuicPacketNumber(i - 2));
  }
}

TEST_F(BandwidthSamplerTest, AckHeightRespectBandwidthEstimateUpperBound) {
  QuicTime::Delta time_between_packets = QuicTime::Delta::FromMilliseconds(10);
  QuicBandwidth first_packet_sending_rate =
      QuicBandwidth::FromBytesAndTimeDelta(kRegularPacketSize,
                                           time_between_packets);

  // Send and ack packet 1.
  SendPacket(1);
  clock_.AdvanceTime(time_between_packets);
  BandwidthSampler::CongestionEventSample sample = OnCongestionEvent({1}, {});
  EXPECT_EQ(first_packet_sending_rate, sample.sample_max_bandwidth);
  EXPECT_EQ(first_packet_sending_rate, max_bandwidth_);

  // Send and ack packet 2, 3 and 4.
  round_trip_count_++;
  est_bandwidth_upper_bound_ = first_packet_sending_rate * 0.3;
  SendPacket(2);
  SendPacket(3);
  SendPacket(4);
  clock_.AdvanceTime(time_between_packets);
  sample = OnCongestionEvent({2, 3, 4}, {});
  EXPECT_EQ(first_packet_sending_rate * 3, sample.sample_max_bandwidth);
  EXPECT_EQ(max_bandwidth_, sample.sample_max_bandwidth);

  EXPECT_LT(2 * kRegularPacketSize, sample.extra_acked);
}

class MaxAckHeightTrackerTest : public QuicTest {
 protected:
  MaxAckHeightTrackerTest() : tracker_(/*initial_filter_window=*/10) {
    if (GetQuicReloadableFlag(
            quic_avoid_overestimate_bandwidth_with_aggregation)) {
      tracker_.SetAckAggregationBandwidthThreshold(1.8);
    }
  }

  // Run a full aggregation episode, which is one or more aggregated acks,
  // followed by a quiet period in which no ack happens.
  // After this function returns, the time is set to the earliest point at which
  // any ack event will cause tracker_.Update() to start a new aggregation.
  void AggregationEpisode(QuicBandwidth aggregation_bandwidth,
                          QuicTime::Delta aggregation_duration,
                          QuicByteCount bytes_per_ack,
                          bool expect_new_aggregation_epoch) {
    ASSERT_GE(aggregation_bandwidth, bandwidth_);
    const QuicTime start_time = now_;

    const QuicByteCount aggregation_bytes =
        aggregation_bandwidth * aggregation_duration;

    const int num_acks = aggregation_bytes / bytes_per_ack;
    ASSERT_EQ(aggregation_bytes, num_acks * bytes_per_ack)
        << "aggregation_bytes: " << aggregation_bytes << " ["
        << aggregation_bandwidth << " in " << aggregation_duration
        << "], bytes_per_ack: " << bytes_per_ack;

    const QuicTime::Delta time_between_acks = QuicTime::Delta::FromMicroseconds(
        aggregation_duration.ToMicroseconds() / num_acks);
    ASSERT_EQ(aggregation_duration, num_acks * time_between_acks)
        << "aggregation_bytes: " << aggregation_bytes
        << ", num_acks: " << num_acks
        << ", time_between_acks: " << time_between_acks;

    // The total duration of aggregation time and quiet period.
    const QuicTime::Delta total_duration = QuicTime::Delta::FromMicroseconds(
        aggregation_bytes * 8 * 1000000 / bandwidth_.ToBitsPerSecond());
    ASSERT_EQ(aggregation_bytes, total_duration * bandwidth_)
        << "total_duration: " << total_duration
        << ", bandwidth_: " << bandwidth_;

    QuicByteCount last_extra_acked = 0;
    for (QuicByteCount bytes = 0; bytes < aggregation_bytes;
         bytes += bytes_per_ack) {
      QuicByteCount extra_acked =
          tracker_.Update(bandwidth_, RoundTripCount(), now_, bytes_per_ack);
      QUIC_VLOG(1) << "T" << now_ << ": Update after " << bytes_per_ack
                   << " bytes acked, " << extra_acked << " extra bytes acked";
      // |extra_acked| should be 0 if either
      // [1] We are at the beginning of a aggregation epoch(bytes==0) and the
      //     the current tracker implementation can identify it, or
      // [2] We are not really aggregating acks.
      if ((bytes == 0 && expect_new_aggregation_epoch) ||  // [1]
          (aggregation_bandwidth == bandwidth_)) {         // [2]
        EXPECT_EQ(0u, extra_acked);
      } else {
        EXPECT_LT(last_extra_acked, extra_acked);
      }
      now_ = now_ + time_between_acks;
      last_extra_acked = extra_acked;
    }

    // Advance past the quiet period.
    const QuicTime time_after_aggregation = now_;
    now_ = start_time + total_duration;
    QUIC_VLOG(1) << "Advanced time from " << time_after_aggregation << " to "
                 << now_ << ". Aggregation time["
                 << (time_after_aggregation - start_time) << "], Quiet time["
                 << (now_ - time_after_aggregation) << "].";
  }

  QuicRoundTripCount RoundTripCount() const {
    return (now_ - QuicTime::Zero()).ToMicroseconds() / rtt_.ToMicroseconds();
  }

  MaxAckHeightTracker tracker_;
  QuicBandwidth bandwidth_ = QuicBandwidth::FromBytesPerSecond(10 * 1000);
  QuicTime now_ = QuicTime::Zero() + QuicTime::Delta::FromMilliseconds(1);
  QuicTime::Delta rtt_ = QuicTime::Delta::FromMilliseconds(60);
};

TEST_F(MaxAckHeightTrackerTest, VeryAggregatedLargeAck) {
  AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6),
                     1200, true);
  AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6),
                     1200, true);
  now_ = now_ - QuicTime::Delta::FromMilliseconds(1);

  if (tracker_.ack_aggregation_bandwidth_threshold() > 1.1) {
    AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6),
                       1200, true);
    EXPECT_EQ(3u, tracker_.num_ack_aggregation_epochs());
  } else {
    AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6),
                       1200, false);
    EXPECT_EQ(2u, tracker_.num_ack_aggregation_epochs());
  }
}

TEST_F(MaxAckHeightTrackerTest, VeryAggregatedSmallAcks) {
  AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6), 300,
                     true);
  AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6), 300,
                     true);
  now_ = now_ - QuicTime::Delta::FromMilliseconds(1);

  if (tracker_.ack_aggregation_bandwidth_threshold() > 1.1) {
    AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6),
                       300, true);
    EXPECT_EQ(3u, tracker_.num_ack_aggregation_epochs());
  } else {
    AggregationEpisode(bandwidth_ * 20, QuicTime::Delta::FromMilliseconds(6),
                       300, false);
    EXPECT_EQ(2u, tracker_.num_ack_aggregation_epochs());
  }
}

TEST_F(MaxAckHeightTrackerTest, SomewhatAggregatedLargeAck) {
  AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50),
                     1000, true);
  AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50),
                     1000, true);
  now_ = now_ - QuicTime::Delta::FromMilliseconds(1);

  if (tracker_.ack_aggregation_bandwidth_threshold() > 1.1) {
    AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50),
                       1000, true);
    EXPECT_EQ(3u, tracker_.num_ack_aggregation_epochs());
  } else {
    AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50),
                       1000, false);
    EXPECT_EQ(2u, tracker_.num_ack_aggregation_epochs());
  }
}

TEST_F(MaxAckHeightTrackerTest, SomewhatAggregatedSmallAcks) {
  AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50), 100,
                     true);
  AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50), 100,
                     true);
  now_ = now_ - QuicTime::Delta::FromMilliseconds(1);

  if (tracker_.ack_aggregation_bandwidth_threshold() > 1.1) {
    AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50),
                       100, true);
    EXPECT_EQ(3u, tracker_.num_ack_aggregation_epochs());
  } else {
    AggregationEpisode(bandwidth_ * 2, QuicTime::Delta::FromMilliseconds(50),
                       100, false);
    EXPECT_EQ(2u, tracker_.num_ack_aggregation_epochs());
  }
}

TEST_F(MaxAckHeightTrackerTest, NotAggregated) {
  AggregationEpisode(bandwidth_, QuicTime::Delta::FromMilliseconds(100), 100,
                     true);
  EXPECT_LT(2u, tracker_.num_ack_aggregation_epochs());
}

}  // namespace test
}  // namespace quic
