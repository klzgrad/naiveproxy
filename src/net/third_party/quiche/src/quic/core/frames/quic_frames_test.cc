// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_ack_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_blocked_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_connection_close_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_goaway_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_mtu_discovery_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_padding_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_ping_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_rst_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stop_waiting_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_window_update_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_interval.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

class QuicFramesTest : public QuicTest {};

TEST_F(QuicFramesTest, AckFrameToString) {
  QuicAckFrame frame;
  frame.largest_acked = QuicPacketNumber(5);
  frame.ack_delay_time = QuicTime::Delta::FromMicroseconds(3);
  frame.packets.Add(QuicPacketNumber(4));
  frame.packets.Add(QuicPacketNumber(5));
  frame.received_packet_times = {
      {QuicPacketNumber(6),
       QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(7)}};
  std::ostringstream stream;
  stream << frame;
  EXPECT_EQ(
      "{ largest_acked: 5, ack_delay_time: 3, packets: [ 4 5  ], "
      "received_packets: [ 6 at 7  ], ecn_counters_populated: 0 }\n",
      stream.str());
  QuicFrame quic_frame(&frame);
  EXPECT_FALSE(IsControlFrame(quic_frame.type));
}

TEST_F(QuicFramesTest, BigAckFrameToString) {
  QuicAckFrame frame;
  frame.largest_acked = QuicPacketNumber(500);
  frame.ack_delay_time = QuicTime::Delta::FromMicroseconds(3);
  frame.packets.AddRange(QuicPacketNumber(4), QuicPacketNumber(501));
  frame.received_packet_times = {
      {QuicPacketNumber(500),
       QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(7)}};
  std::ostringstream stream;
  stream << frame;
  EXPECT_EQ(
      "{ largest_acked: 500, ack_delay_time: 3, packets: [ 4...500  ], "
      "received_packets: [ 500 at 7  ], ecn_counters_populated: 0 }\n",
      stream.str());
  QuicFrame quic_frame(&frame);
  EXPECT_FALSE(IsControlFrame(quic_frame.type));
}

TEST_F(QuicFramesTest, PaddingFrameToString) {
  QuicPaddingFrame frame;
  frame.num_padding_bytes = 1;
  std::ostringstream stream;
  stream << frame;
  EXPECT_EQ("{ num_padding_bytes: 1 }\n", stream.str());
  QuicFrame quic_frame(frame);
  EXPECT_FALSE(IsControlFrame(quic_frame.type));
}

TEST_F(QuicFramesTest, RstStreamFrameToString) {
  QuicRstStreamFrame rst_stream;
  QuicFrame frame(&rst_stream);
  SetControlFrameId(1, &frame);
  EXPECT_EQ(1u, GetControlFrameId(frame));
  rst_stream.stream_id = 1;
  rst_stream.byte_offset = 3;
  rst_stream.error_code = QUIC_STREAM_CANCELLED;
  std::ostringstream stream;
  stream << rst_stream;
  EXPECT_EQ(
      "{ control_frame_id: 1, stream_id: 1, byte_offset: 3, error_code: 6 }\n",
      stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, StopSendingFrameToString) {
  QuicStopSendingFrame stop_sending;
  QuicFrame frame(&stop_sending);
  SetControlFrameId(1, &frame);
  EXPECT_EQ(1u, GetControlFrameId(frame));
  stop_sending.stream_id = 321;
  stop_sending.application_error_code = QUIC_STREAM_CANCELLED;
  std::ostringstream stream;
  stream << stop_sending;
  EXPECT_EQ(
      "{ control_frame_id: 1, stream_id: 321, application_error_code: 6 }\n",
      stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, StreamsBlockedFrameToString) {
  QuicStreamsBlockedFrame streams_blocked;
  QuicFrame frame(streams_blocked);
  SetControlFrameId(1, &frame);
  EXPECT_EQ(1u, GetControlFrameId(frame));
  // QuicStreamsBlocked is copied into a QuicFrame (as opposed to putting a
  // pointer to it into QuicFrame) so need to work with the copy in |frame| and
  // not the original one, streams_blocked.
  frame.streams_blocked_frame.stream_count = 321;
  frame.streams_blocked_frame.unidirectional = false;
  std::ostringstream stream;
  stream << frame.streams_blocked_frame;
  EXPECT_EQ("{ control_frame_id: 1, stream count: 321, bidirectional }\n",
            stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, MaxStreamsFrameToString) {
  QuicMaxStreamsFrame max_streams;
  QuicFrame frame(max_streams);
  SetControlFrameId(1, &frame);
  EXPECT_EQ(1u, GetControlFrameId(frame));
  // QuicMaxStreams is copied into a QuicFrame (as opposed to putting a
  // pointer to it into QuicFrame) so need to work with the copy in |frame| and
  // not the original one, max_streams.
  frame.max_streams_frame.stream_count = 321;
  frame.max_streams_frame.unidirectional = true;
  std::ostringstream stream;
  stream << frame.max_streams_frame;
  EXPECT_EQ("{ control_frame_id: 1, stream_count: 321, unidirectional }\n",
            stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, ConnectionCloseFrameToString) {
  QuicConnectionCloseFrame frame;
  frame.quic_error_code = QUIC_NETWORK_IDLE_TIMEOUT;
  frame.error_details = "No recent network activity.";
  std::ostringstream stream;
  stream << frame;
  // Note that "extracted_error_code: 122" is QUIC_IETF_GQUIC_ERROR_MISSING,
  // indicating that, in fact, no extended error code was available from the
  // underlying frame.
  EXPECT_EQ(
      "{ Close type: GOOGLE_QUIC_CONNECTION_CLOSE, error_code: 25, "
      "extracted_error_code: QUIC_NO_ERROR, "
      "error_details: 'No recent "
      "network activity.'"
      "}\n",
      stream.str());
  QuicFrame quic_frame(&frame);
  EXPECT_FALSE(IsControlFrame(quic_frame.type));
}

TEST_F(QuicFramesTest, TransportConnectionCloseFrameToString) {
  QuicConnectionCloseFrame frame;
  frame.close_type = IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
  frame.transport_error_code = FINAL_SIZE_ERROR;
  frame.extracted_error_code = QUIC_NETWORK_IDLE_TIMEOUT;
  frame.error_details = "No recent network activity.";
  frame.transport_close_frame_type = IETF_STREAM;
  std::ostringstream stream;
  stream << frame;
  EXPECT_EQ(
      "{ Close type: IETF_QUIC_TRANSPORT_CONNECTION_CLOSE, error_code: "
      "FINAL_SIZE_ERROR, "
      "extracted_error_code: QUIC_NETWORK_IDLE_TIMEOUT, "
      "error_details: 'No recent "
      "network activity.', "
      "frame_type: IETF_STREAM"
      "}\n",
      stream.str());
  QuicFrame quic_frame(&frame);
  EXPECT_FALSE(IsControlFrame(quic_frame.type));
}

TEST_F(QuicFramesTest, GoAwayFrameToString) {
  QuicGoAwayFrame goaway_frame;
  QuicFrame frame(&goaway_frame);
  SetControlFrameId(2, &frame);
  EXPECT_EQ(2u, GetControlFrameId(frame));
  goaway_frame.error_code = QUIC_NETWORK_IDLE_TIMEOUT;
  goaway_frame.last_good_stream_id = 2;
  goaway_frame.reason_phrase = "Reason";
  std::ostringstream stream;
  stream << goaway_frame;
  EXPECT_EQ(
      "{ control_frame_id: 2, error_code: 25, last_good_stream_id: 2, "
      "reason_phrase: "
      "'Reason' }\n",
      stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, WindowUpdateFrameToString) {
  QuicWindowUpdateFrame window_update;
  QuicFrame frame(&window_update);
  SetControlFrameId(3, &frame);
  EXPECT_EQ(3u, GetControlFrameId(frame));
  std::ostringstream stream;
  window_update.stream_id = 1;
  window_update.max_data = 2;
  stream << window_update;
  EXPECT_EQ("{ control_frame_id: 3, stream_id: 1, max_data: 2 }\n",
            stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, BlockedFrameToString) {
  QuicBlockedFrame blocked;
  QuicFrame frame(&blocked);
  SetControlFrameId(4, &frame);
  EXPECT_EQ(4u, GetControlFrameId(frame));
  blocked.stream_id = 1;
  std::ostringstream stream;
  stream << blocked;
  EXPECT_EQ("{ control_frame_id: 4, stream_id: 1 }\n", stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, PingFrameToString) {
  QuicPingFrame ping;
  QuicFrame frame(ping);
  SetControlFrameId(5, &frame);
  EXPECT_EQ(5u, GetControlFrameId(frame));
  std::ostringstream stream;
  stream << frame.ping_frame;
  EXPECT_EQ("{ control_frame_id: 5 }\n", stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, HandshakeDoneFrameToString) {
  QuicHandshakeDoneFrame handshake_done;
  QuicFrame frame(handshake_done);
  SetControlFrameId(6, &frame);
  EXPECT_EQ(6u, GetControlFrameId(frame));
  std::ostringstream stream;
  stream << frame.handshake_done_frame;
  EXPECT_EQ("{ control_frame_id: 6 }\n", stream.str());
  EXPECT_TRUE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, StreamFrameToString) {
  QuicStreamFrame frame;
  frame.stream_id = 1;
  frame.fin = false;
  frame.offset = 2;
  frame.data_length = 3;
  std::ostringstream stream;
  stream << frame;
  EXPECT_EQ("{ stream_id: 1, fin: 0, offset: 2, length: 3 }\n", stream.str());
  EXPECT_FALSE(IsControlFrame(frame.type));
}

TEST_F(QuicFramesTest, StopWaitingFrameToString) {
  QuicStopWaitingFrame frame;
  frame.least_unacked = QuicPacketNumber(2);
  std::ostringstream stream;
  stream << frame;
  EXPECT_EQ("{ least_unacked: 2 }\n", stream.str());
  QuicFrame quic_frame(frame);
  EXPECT_FALSE(IsControlFrame(quic_frame.type));
}

TEST_F(QuicFramesTest, IsAwaitingPacket) {
  QuicAckFrame ack_frame1;
  ack_frame1.largest_acked = QuicPacketNumber(10u);
  ack_frame1.packets.AddRange(QuicPacketNumber(1), QuicPacketNumber(11));
  EXPECT_TRUE(
      IsAwaitingPacket(ack_frame1, QuicPacketNumber(11u), QuicPacketNumber()));
  EXPECT_FALSE(
      IsAwaitingPacket(ack_frame1, QuicPacketNumber(1u), QuicPacketNumber()));

  ack_frame1.packets.Add(QuicPacketNumber(12));
  EXPECT_TRUE(
      IsAwaitingPacket(ack_frame1, QuicPacketNumber(11u), QuicPacketNumber()));

  QuicAckFrame ack_frame2;
  ack_frame2.largest_acked = QuicPacketNumber(100u);
  ack_frame2.packets.AddRange(QuicPacketNumber(21), QuicPacketNumber(100));
  EXPECT_FALSE(IsAwaitingPacket(ack_frame2, QuicPacketNumber(11u),
                                QuicPacketNumber(20u)));
  EXPECT_FALSE(IsAwaitingPacket(ack_frame2, QuicPacketNumber(80u),
                                QuicPacketNumber(20u)));
  EXPECT_TRUE(IsAwaitingPacket(ack_frame2, QuicPacketNumber(101u),
                               QuicPacketNumber(20u)));

  ack_frame2.packets.AddRange(QuicPacketNumber(102), QuicPacketNumber(200));
  EXPECT_TRUE(IsAwaitingPacket(ack_frame2, QuicPacketNumber(101u),
                               QuicPacketNumber(20u)));
}

TEST_F(QuicFramesTest, AddPacket) {
  QuicAckFrame ack_frame1;
  ack_frame1.packets.Add(QuicPacketNumber(1));
  ack_frame1.packets.Add(QuicPacketNumber(99));

  EXPECT_EQ(2u, ack_frame1.packets.NumIntervals());
  EXPECT_EQ(QuicPacketNumber(1u), ack_frame1.packets.Min());
  EXPECT_EQ(QuicPacketNumber(99u), ack_frame1.packets.Max());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals;
  expected_intervals.emplace_back(
      QuicInterval<QuicPacketNumber>(QuicPacketNumber(1), QuicPacketNumber(2)));
  expected_intervals.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(99), QuicPacketNumber(100)));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  EXPECT_EQ(expected_intervals, actual_intervals);

  ack_frame1.packets.Add(QuicPacketNumber(20));
  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals2(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals2;
  expected_intervals2.emplace_back(
      QuicInterval<QuicPacketNumber>(QuicPacketNumber(1), QuicPacketNumber(2)));
  expected_intervals2.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(20), QuicPacketNumber(21)));
  expected_intervals2.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(99), QuicPacketNumber(100)));

  EXPECT_EQ(3u, ack_frame1.packets.NumIntervals());
  EXPECT_EQ(expected_intervals2, actual_intervals2);

  ack_frame1.packets.Add(QuicPacketNumber(19));
  ack_frame1.packets.Add(QuicPacketNumber(21));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals3(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals3;
  expected_intervals3.emplace_back(
      QuicInterval<QuicPacketNumber>(QuicPacketNumber(1), QuicPacketNumber(2)));
  expected_intervals3.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(19), QuicPacketNumber(22)));
  expected_intervals3.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(99), QuicPacketNumber(100)));

  EXPECT_EQ(expected_intervals3, actual_intervals3);

  ack_frame1.packets.Add(QuicPacketNumber(20));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals4(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  EXPECT_EQ(expected_intervals3, actual_intervals4);

  QuicAckFrame ack_frame2;
  ack_frame2.packets.Add(QuicPacketNumber(20));
  ack_frame2.packets.Add(QuicPacketNumber(40));
  ack_frame2.packets.Add(QuicPacketNumber(60));
  ack_frame2.packets.Add(QuicPacketNumber(10));
  ack_frame2.packets.Add(QuicPacketNumber(80));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals5(
      ack_frame2.packets.begin(), ack_frame2.packets.end());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals5;
  expected_intervals5.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(10), QuicPacketNumber(11)));
  expected_intervals5.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(20), QuicPacketNumber(21)));
  expected_intervals5.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(40), QuicPacketNumber(41)));
  expected_intervals5.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(60), QuicPacketNumber(61)));
  expected_intervals5.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(80), QuicPacketNumber(81)));

  EXPECT_EQ(expected_intervals5, actual_intervals5);
}

TEST_F(QuicFramesTest, AddInterval) {
  QuicAckFrame ack_frame1;
  ack_frame1.packets.AddRange(QuicPacketNumber(1), QuicPacketNumber(10));
  ack_frame1.packets.AddRange(QuicPacketNumber(50), QuicPacketNumber(100));

  EXPECT_EQ(2u, ack_frame1.packets.NumIntervals());
  EXPECT_EQ(QuicPacketNumber(1u), ack_frame1.packets.Min());
  EXPECT_EQ(QuicPacketNumber(99u), ack_frame1.packets.Max());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals{
      {QuicPacketNumber(1), QuicPacketNumber(10)},
      {QuicPacketNumber(50), QuicPacketNumber(100)},
  };

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  EXPECT_EQ(expected_intervals, actual_intervals);

  // Add a range in the middle.
  ack_frame1.packets.AddRange(QuicPacketNumber(20), QuicPacketNumber(30));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals2(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals2{
      {QuicPacketNumber(1), QuicPacketNumber(10)},
      {QuicPacketNumber(20), QuicPacketNumber(30)},
      {QuicPacketNumber(50), QuicPacketNumber(100)},
  };

  EXPECT_EQ(expected_intervals2.size(), ack_frame1.packets.NumIntervals());
  EXPECT_EQ(expected_intervals2, actual_intervals2);

  // Add ranges at both ends.
  QuicAckFrame ack_frame2;
  ack_frame2.packets.AddRange(QuicPacketNumber(20), QuicPacketNumber(25));
  ack_frame2.packets.AddRange(QuicPacketNumber(40), QuicPacketNumber(45));
  ack_frame2.packets.AddRange(QuicPacketNumber(60), QuicPacketNumber(65));
  ack_frame2.packets.AddRange(QuicPacketNumber(10), QuicPacketNumber(15));
  ack_frame2.packets.AddRange(QuicPacketNumber(80), QuicPacketNumber(85));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals8(
      ack_frame2.packets.begin(), ack_frame2.packets.end());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals8{
      {QuicPacketNumber(10), QuicPacketNumber(15)},
      {QuicPacketNumber(20), QuicPacketNumber(25)},
      {QuicPacketNumber(40), QuicPacketNumber(45)},
      {QuicPacketNumber(60), QuicPacketNumber(65)},
      {QuicPacketNumber(80), QuicPacketNumber(85)},
  };

  EXPECT_EQ(expected_intervals8, actual_intervals8);
}

TEST_F(QuicFramesTest, AddAdjacentForward) {
  QuicAckFrame ack_frame1;
  ack_frame1.packets.Add(QuicPacketNumber(49));
  ack_frame1.packets.AddRange(QuicPacketNumber(50), QuicPacketNumber(60));
  ack_frame1.packets.AddRange(QuicPacketNumber(60), QuicPacketNumber(70));
  ack_frame1.packets.AddRange(QuicPacketNumber(70), QuicPacketNumber(100));

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals;
  expected_intervals.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(49), QuicPacketNumber(100)));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  EXPECT_EQ(expected_intervals, actual_intervals);
}

TEST_F(QuicFramesTest, AddAdjacentReverse) {
  QuicAckFrame ack_frame1;
  ack_frame1.packets.AddRange(QuicPacketNumber(70), QuicPacketNumber(100));
  ack_frame1.packets.AddRange(QuicPacketNumber(60), QuicPacketNumber(70));
  ack_frame1.packets.AddRange(QuicPacketNumber(50), QuicPacketNumber(60));
  ack_frame1.packets.Add(QuicPacketNumber(49));

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals;
  expected_intervals.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(49), QuicPacketNumber(100)));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals(
      ack_frame1.packets.begin(), ack_frame1.packets.end());

  EXPECT_EQ(expected_intervals, actual_intervals);
}

TEST_F(QuicFramesTest, RemoveSmallestInterval) {
  QuicAckFrame ack_frame1;
  ack_frame1.largest_acked = QuicPacketNumber(100u);
  ack_frame1.packets.AddRange(QuicPacketNumber(51), QuicPacketNumber(60));
  ack_frame1.packets.AddRange(QuicPacketNumber(71), QuicPacketNumber(80));
  ack_frame1.packets.AddRange(QuicPacketNumber(91), QuicPacketNumber(100));
  ack_frame1.packets.RemoveSmallestInterval();
  EXPECT_EQ(2u, ack_frame1.packets.NumIntervals());
  EXPECT_EQ(QuicPacketNumber(71u), ack_frame1.packets.Min());
  EXPECT_EQ(QuicPacketNumber(99u), ack_frame1.packets.Max());

  ack_frame1.packets.RemoveSmallestInterval();
  EXPECT_EQ(1u, ack_frame1.packets.NumIntervals());
  EXPECT_EQ(QuicPacketNumber(91u), ack_frame1.packets.Min());
  EXPECT_EQ(QuicPacketNumber(99u), ack_frame1.packets.Max());
}

TEST_F(QuicFramesTest, CopyQuicFrames) {
  QuicFrames frames;
  SimpleBufferAllocator allocator;
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  QuicMessageFrame* message_frame =
      new QuicMessageFrame(1, MakeSpan(&allocator, "message", &storage));
  // Construct a frame list.
  for (uint8_t i = 0; i < NUM_FRAME_TYPES; ++i) {
    switch (i) {
      case PADDING_FRAME:
        frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
        break;
      case RST_STREAM_FRAME:
        frames.push_back(QuicFrame(new QuicRstStreamFrame()));
        break;
      case CONNECTION_CLOSE_FRAME:
        frames.push_back(QuicFrame(new QuicConnectionCloseFrame()));
        break;
      case GOAWAY_FRAME:
        frames.push_back(QuicFrame(new QuicGoAwayFrame()));
        break;
      case WINDOW_UPDATE_FRAME:
        frames.push_back(QuicFrame(new QuicWindowUpdateFrame()));
        break;
      case BLOCKED_FRAME:
        frames.push_back(QuicFrame(new QuicBlockedFrame()));
        break;
      case STOP_WAITING_FRAME:
        frames.push_back(QuicFrame(QuicStopWaitingFrame()));
        break;
      case PING_FRAME:
        frames.push_back(QuicFrame(QuicPingFrame()));
        break;
      case CRYPTO_FRAME:
        frames.push_back(QuicFrame(new QuicCryptoFrame()));
        break;
      case STREAM_FRAME:
        frames.push_back(QuicFrame(QuicStreamFrame()));
        break;
      case ACK_FRAME:
        frames.push_back(QuicFrame(new QuicAckFrame()));
        break;
      case MTU_DISCOVERY_FRAME:
        frames.push_back(QuicFrame(QuicMtuDiscoveryFrame()));
        break;
      case NEW_CONNECTION_ID_FRAME:
        frames.push_back(QuicFrame(new QuicNewConnectionIdFrame()));
        break;
      case MAX_STREAMS_FRAME:
        frames.push_back(QuicFrame(QuicMaxStreamsFrame()));
        break;
      case STREAMS_BLOCKED_FRAME:
        frames.push_back(QuicFrame(QuicStreamsBlockedFrame()));
        break;
      case PATH_RESPONSE_FRAME:
        frames.push_back(QuicFrame(new QuicPathResponseFrame()));
        break;
      case PATH_CHALLENGE_FRAME:
        frames.push_back(QuicFrame(new QuicPathChallengeFrame()));
        break;
      case STOP_SENDING_FRAME:
        frames.push_back(QuicFrame(new QuicStopSendingFrame()));
        break;
      case MESSAGE_FRAME:
        frames.push_back(QuicFrame(message_frame));
        break;
      case NEW_TOKEN_FRAME:
        frames.push_back(QuicFrame(new QuicNewTokenFrame()));
        break;
      case RETIRE_CONNECTION_ID_FRAME:
        frames.push_back(QuicFrame(new QuicRetireConnectionIdFrame()));
        break;
      case HANDSHAKE_DONE_FRAME:
        frames.push_back(QuicFrame(QuicHandshakeDoneFrame()));
        break;
      default:
        ASSERT_TRUE(false)
            << "Please fix CopyQuicFrames if a new frame type is added.";
        break;
    }
  }

  QuicFrames copy = CopyQuicFrames(&allocator, frames);
  ASSERT_EQ(NUM_FRAME_TYPES, copy.size());
  for (uint8_t i = 0; i < NUM_FRAME_TYPES; ++i) {
    EXPECT_EQ(i, copy[i].type);
    if (i != MESSAGE_FRAME) {
      continue;
    }
    // Verify message frame is correctly copied.
    EXPECT_EQ(1u, copy[i].message_frame->message_id);
    EXPECT_EQ(nullptr, copy[i].message_frame->data);
    EXPECT_EQ(7u, copy[i].message_frame->message_length);
    ASSERT_EQ(1u, copy[i].message_frame->message_data.size());
    EXPECT_EQ(0, memcmp(copy[i].message_frame->message_data[0].data(),
                        frames[i].message_frame->message_data[0].data(), 7));
  }
  DeleteFrames(&frames);
  DeleteFrames(&copy);
}

class PacketNumberQueueTest : public QuicTest {};

// Tests that a queue contains the expected data after calls to Add().
TEST_F(PacketNumberQueueTest, AddRange) {
  PacketNumberQueue queue;
  queue.AddRange(QuicPacketNumber(1), QuicPacketNumber(51));
  queue.Add(QuicPacketNumber(53));

  EXPECT_FALSE(queue.Contains(QuicPacketNumber()));
  for (int i = 1; i < 51; ++i) {
    EXPECT_TRUE(queue.Contains(QuicPacketNumber(i)));
  }
  EXPECT_FALSE(queue.Contains(QuicPacketNumber(51)));
  EXPECT_FALSE(queue.Contains(QuicPacketNumber(52)));
  EXPECT_TRUE(queue.Contains(QuicPacketNumber(53)));
  EXPECT_FALSE(queue.Contains(QuicPacketNumber(54)));
  EXPECT_EQ(51u, queue.NumPacketsSlow());
  EXPECT_EQ(QuicPacketNumber(1u), queue.Min());
  EXPECT_EQ(QuicPacketNumber(53u), queue.Max());

  queue.Add(QuicPacketNumber(70));
  EXPECT_EQ(QuicPacketNumber(70u), queue.Max());
}

// Tests Contains function
TEST_F(PacketNumberQueueTest, Contains) {
  PacketNumberQueue queue;
  EXPECT_FALSE(queue.Contains(QuicPacketNumber()));
  queue.AddRange(QuicPacketNumber(5), QuicPacketNumber(10));
  queue.Add(QuicPacketNumber(20));

  for (int i = 1; i < 5; ++i) {
    EXPECT_FALSE(queue.Contains(QuicPacketNumber(i)));
  }

  for (int i = 5; i < 10; ++i) {
    EXPECT_TRUE(queue.Contains(QuicPacketNumber(i)));
  }
  for (int i = 10; i < 20; ++i) {
    EXPECT_FALSE(queue.Contains(QuicPacketNumber(i)));
  }
  EXPECT_TRUE(queue.Contains(QuicPacketNumber(20)));
  EXPECT_FALSE(queue.Contains(QuicPacketNumber(21)));

  PacketNumberQueue queue2;
  EXPECT_FALSE(queue2.Contains(QuicPacketNumber(1)));
  for (int i = 1; i < 51; ++i) {
    queue2.Add(QuicPacketNumber(2 * i));
  }
  EXPECT_FALSE(queue2.Contains(QuicPacketNumber()));
  for (int i = 1; i < 51; ++i) {
    if (i % 2 == 0) {
      EXPECT_TRUE(queue2.Contains(QuicPacketNumber(i)));
    } else {
      EXPECT_FALSE(queue2.Contains(QuicPacketNumber(i)));
    }
  }
  EXPECT_FALSE(queue2.Contains(QuicPacketNumber(101)));
}

// Tests that a queue contains the expected data after calls to RemoveUpTo().
TEST_F(PacketNumberQueueTest, Removal) {
  PacketNumberQueue queue;
  EXPECT_FALSE(queue.Contains(QuicPacketNumber(51)));
  queue.AddRange(QuicPacketNumber(1), QuicPacketNumber(100));

  EXPECT_TRUE(queue.RemoveUpTo(QuicPacketNumber(51)));
  EXPECT_FALSE(queue.RemoveUpTo(QuicPacketNumber(51)));

  EXPECT_FALSE(queue.Contains(QuicPacketNumber()));
  for (int i = 1; i < 51; ++i) {
    EXPECT_FALSE(queue.Contains(QuicPacketNumber(i)));
  }
  for (int i = 51; i < 100; ++i) {
    EXPECT_TRUE(queue.Contains(QuicPacketNumber(i)));
  }
  EXPECT_EQ(49u, queue.NumPacketsSlow());
  EXPECT_EQ(QuicPacketNumber(51u), queue.Min());
  EXPECT_EQ(QuicPacketNumber(99u), queue.Max());

  PacketNumberQueue queue2;
  queue2.AddRange(QuicPacketNumber(1), QuicPacketNumber(5));
  EXPECT_TRUE(queue2.RemoveUpTo(QuicPacketNumber(3)));
  EXPECT_TRUE(queue2.RemoveUpTo(QuicPacketNumber(50)));
  EXPECT_TRUE(queue2.Empty());
}

// Tests that a queue is empty when all of its elements are removed.
TEST_F(PacketNumberQueueTest, Empty) {
  PacketNumberQueue queue;
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(0u, queue.NumPacketsSlow());

  queue.AddRange(QuicPacketNumber(1), QuicPacketNumber(100));
  EXPECT_TRUE(queue.RemoveUpTo(QuicPacketNumber(100)));
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(0u, queue.NumPacketsSlow());
}

// Tests that logging the state of a PacketNumberQueue does not crash.
TEST_F(PacketNumberQueueTest, LogDoesNotCrash) {
  std::ostringstream oss;
  PacketNumberQueue queue;
  oss << queue;

  queue.Add(QuicPacketNumber(1));
  queue.AddRange(QuicPacketNumber(50), QuicPacketNumber(100));
  oss << queue;
}

// Tests that the iterators returned from a packet queue iterate over the queue.
TEST_F(PacketNumberQueueTest, Iterators) {
  PacketNumberQueue queue;
  queue.AddRange(QuicPacketNumber(1), QuicPacketNumber(100));

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals(
      queue.begin(), queue.end());

  PacketNumberQueue queue2;
  for (int i = 1; i < 100; i++) {
    queue2.AddRange(QuicPacketNumber(i), QuicPacketNumber(i + 1));
  }

  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals2(
      queue2.begin(), queue2.end());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals;
  expected_intervals.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(1), QuicPacketNumber(100)));
  EXPECT_EQ(expected_intervals, actual_intervals);
  EXPECT_EQ(expected_intervals, actual_intervals2);
  EXPECT_EQ(actual_intervals, actual_intervals2);
}

TEST_F(PacketNumberQueueTest, ReversedIterators) {
  PacketNumberQueue queue;
  queue.AddRange(QuicPacketNumber(1), QuicPacketNumber(100));
  PacketNumberQueue queue2;
  for (int i = 1; i < 100; i++) {
    queue2.AddRange(QuicPacketNumber(i), QuicPacketNumber(i + 1));
  }
  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals(
      queue.rbegin(), queue.rend());
  const std::vector<QuicInterval<QuicPacketNumber>> actual_intervals2(
      queue2.rbegin(), queue2.rend());

  std::vector<QuicInterval<QuicPacketNumber>> expected_intervals;
  expected_intervals.emplace_back(QuicInterval<QuicPacketNumber>(
      QuicPacketNumber(1), QuicPacketNumber(100)));

  EXPECT_EQ(expected_intervals, actual_intervals);
  EXPECT_EQ(expected_intervals, actual_intervals2);
  EXPECT_EQ(actual_intervals, actual_intervals2);

  PacketNumberQueue queue3;
  for (int i = 1; i < 20; i++) {
    queue3.Add(QuicPacketNumber(2 * i));
  }

  auto begin = queue3.begin();
  auto end = queue3.end();
  --end;
  auto rbegin = queue3.rbegin();
  auto rend = queue3.rend();
  --rend;

  EXPECT_EQ(*begin, *rend);
  EXPECT_EQ(*rbegin, *end);
}

TEST_F(PacketNumberQueueTest, IntervalLengthAndRemoveInterval) {
  PacketNumberQueue queue;
  queue.AddRange(QuicPacketNumber(1), QuicPacketNumber(10));
  queue.AddRange(QuicPacketNumber(20), QuicPacketNumber(30));
  queue.AddRange(QuicPacketNumber(40), QuicPacketNumber(50));
  EXPECT_EQ(3u, queue.NumIntervals());
  EXPECT_EQ(10u, queue.LastIntervalLength());

  EXPECT_TRUE(queue.RemoveUpTo(QuicPacketNumber(25)));
  EXPECT_EQ(2u, queue.NumIntervals());
  EXPECT_EQ(10u, queue.LastIntervalLength());
  EXPECT_EQ(QuicPacketNumber(25u), queue.Min());
  EXPECT_EQ(QuicPacketNumber(49u), queue.Max());
}

}  // namespace
}  // namespace test
}  // namespace quic
