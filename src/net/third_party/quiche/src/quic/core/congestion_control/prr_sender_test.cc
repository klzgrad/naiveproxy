// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/congestion_control/prr_sender.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

namespace {
// Constant based on TCP defaults.
const QuicByteCount kMaxSegmentSize = kDefaultTCPMSS;
}  // namespace

class PrrSenderTest : public QuicTest {};

TEST_F(PrrSenderTest, SingleLossResultsInSendOnEveryOtherAck) {
  PrrSender prr;
  QuicPacketCount num_packets_in_flight = 50;
  QuicByteCount bytes_in_flight = num_packets_in_flight * kMaxSegmentSize;
  const QuicPacketCount ssthresh_after_loss = num_packets_in_flight / 2;
  const QuicByteCount congestion_window = ssthresh_after_loss * kMaxSegmentSize;

  prr.OnPacketLost(bytes_in_flight);
  // Ack a packet. PRR allows one packet to leave immediately.
  prr.OnPacketAcked(kMaxSegmentSize);
  bytes_in_flight -= kMaxSegmentSize;
  EXPECT_TRUE(prr.CanSend(congestion_window, bytes_in_flight,
                          ssthresh_after_loss * kMaxSegmentSize));
  // Send retransmission.
  prr.OnPacketSent(kMaxSegmentSize);
  // PRR shouldn't allow sending any more packets.
  EXPECT_FALSE(prr.CanSend(congestion_window, bytes_in_flight,
                           ssthresh_after_loss * kMaxSegmentSize));

  // One packet is lost, and one ack was consumed above. PRR now paces
  // transmissions through the remaining 48 acks. PRR will alternatively
  // disallow and allow a packet to be sent in response to an ack.
  for (uint64_t i = 0; i < ssthresh_after_loss - 1; ++i) {
    // Ack a packet. PRR shouldn't allow sending a packet in response.
    prr.OnPacketAcked(kMaxSegmentSize);
    bytes_in_flight -= kMaxSegmentSize;
    EXPECT_FALSE(prr.CanSend(congestion_window, bytes_in_flight,
                             ssthresh_after_loss * kMaxSegmentSize));
    // Ack another packet. PRR should now allow sending a packet in response.
    prr.OnPacketAcked(kMaxSegmentSize);
    bytes_in_flight -= kMaxSegmentSize;
    EXPECT_TRUE(prr.CanSend(congestion_window, bytes_in_flight,
                            ssthresh_after_loss * kMaxSegmentSize));
    // Send a packet in response.
    prr.OnPacketSent(kMaxSegmentSize);
    bytes_in_flight += kMaxSegmentSize;
  }

  // Since bytes_in_flight is now equal to congestion_window, PRR now maintains
  // packet conservation, allowing one packet to be sent in response to an ack.
  EXPECT_EQ(congestion_window, bytes_in_flight);
  for (int i = 0; i < 10; ++i) {
    // Ack a packet.
    prr.OnPacketAcked(kMaxSegmentSize);
    bytes_in_flight -= kMaxSegmentSize;
    EXPECT_TRUE(prr.CanSend(congestion_window, bytes_in_flight,
                            ssthresh_after_loss * kMaxSegmentSize));
    // Send a packet in response, since PRR allows it.
    prr.OnPacketSent(kMaxSegmentSize);
    bytes_in_flight += kMaxSegmentSize;

    // Since bytes_in_flight is equal to the congestion_window,
    // PRR disallows sending.
    EXPECT_EQ(congestion_window, bytes_in_flight);
    EXPECT_FALSE(prr.CanSend(congestion_window, bytes_in_flight,
                             ssthresh_after_loss * kMaxSegmentSize));
  }
}

TEST_F(PrrSenderTest, BurstLossResultsInSlowStart) {
  PrrSender prr;
  QuicByteCount bytes_in_flight = 20 * kMaxSegmentSize;
  const QuicPacketCount num_packets_lost = 13;
  const QuicPacketCount ssthresh_after_loss = 10;
  const QuicByteCount congestion_window = ssthresh_after_loss * kMaxSegmentSize;

  // Lose 13 packets.
  bytes_in_flight -= num_packets_lost * kMaxSegmentSize;
  prr.OnPacketLost(bytes_in_flight);

  // PRR-SSRB will allow the following 3 acks to send up to 2 packets.
  for (int i = 0; i < 3; ++i) {
    prr.OnPacketAcked(kMaxSegmentSize);
    bytes_in_flight -= kMaxSegmentSize;
    // PRR-SSRB should allow two packets to be sent.
    for (int j = 0; j < 2; ++j) {
      EXPECT_TRUE(prr.CanSend(congestion_window, bytes_in_flight,
                              ssthresh_after_loss * kMaxSegmentSize));
      // Send a packet in response.
      prr.OnPacketSent(kMaxSegmentSize);
      bytes_in_flight += kMaxSegmentSize;
    }
    // PRR should allow no more than 2 packets in response to an ack.
    EXPECT_FALSE(prr.CanSend(congestion_window, bytes_in_flight,
                             ssthresh_after_loss * kMaxSegmentSize));
  }

  // Out of SSRB mode, PRR allows one send in response to each ack.
  for (int i = 0; i < 10; ++i) {
    prr.OnPacketAcked(kMaxSegmentSize);
    bytes_in_flight -= kMaxSegmentSize;
    EXPECT_TRUE(prr.CanSend(congestion_window, bytes_in_flight,
                            ssthresh_after_loss * kMaxSegmentSize));
    // Send a packet in response.
    prr.OnPacketSent(kMaxSegmentSize);
    bytes_in_flight += kMaxSegmentSize;
  }
}

}  // namespace test
}  // namespace quic
