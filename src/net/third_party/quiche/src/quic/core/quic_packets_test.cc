// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packets.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

namespace quic {
namespace test {
namespace {

QuicPacketHeader CreateFakePacketHeader() {
  QuicPacketHeader header;
  header.destination_connection_id = TestConnectionId(1);
  header.destination_connection_id_included = CONNECTION_ID_PRESENT;
  header.source_connection_id = TestConnectionId(2);
  header.source_connection_id_included = CONNECTION_ID_ABSENT;
  return header;
}

class QuicPacketsTest : public QuicTest {};

TEST_F(QuicPacketsTest, GetServerConnectionIdAsRecipient) {
  QuicPacketHeader header = CreateFakePacketHeader();
  EXPECT_EQ(TestConnectionId(1),
            GetServerConnectionIdAsRecipient(header, Perspective::IS_SERVER));
  EXPECT_EQ(TestConnectionId(2),
            GetServerConnectionIdAsRecipient(header, Perspective::IS_CLIENT));
}

TEST_F(QuicPacketsTest, GetServerConnectionIdAsSender) {
  QuicPacketHeader header = CreateFakePacketHeader();
  EXPECT_EQ(TestConnectionId(2),
            GetServerConnectionIdAsSender(header, Perspective::IS_SERVER));
  EXPECT_EQ(TestConnectionId(1),
            GetServerConnectionIdAsSender(header, Perspective::IS_CLIENT));
}

TEST_F(QuicPacketsTest, GetServerConnectionIdIncludedAsSender) {
  QuicPacketHeader header = CreateFakePacketHeader();
  EXPECT_EQ(CONNECTION_ID_ABSENT, GetServerConnectionIdIncludedAsSender(
                                      header, Perspective::IS_SERVER));
  EXPECT_EQ(CONNECTION_ID_PRESENT, GetServerConnectionIdIncludedAsSender(
                                       header, Perspective::IS_CLIENT));
}

TEST_F(QuicPacketsTest, GetClientConnectionIdIncludedAsSender) {
  QuicPacketHeader header = CreateFakePacketHeader();
  EXPECT_EQ(CONNECTION_ID_PRESENT, GetClientConnectionIdIncludedAsSender(
                                       header, Perspective::IS_SERVER));
  EXPECT_EQ(CONNECTION_ID_ABSENT, GetClientConnectionIdIncludedAsSender(
                                      header, Perspective::IS_CLIENT));
}

TEST_F(QuicPacketsTest, GetClientConnectionIdAsRecipient) {
  QuicPacketHeader header = CreateFakePacketHeader();
  EXPECT_EQ(TestConnectionId(2),
            GetClientConnectionIdAsRecipient(header, Perspective::IS_SERVER));
  EXPECT_EQ(TestConnectionId(1),
            GetClientConnectionIdAsRecipient(header, Perspective::IS_CLIENT));
}

TEST_F(QuicPacketsTest, GetClientConnectionIdAsSender) {
  QuicPacketHeader header = CreateFakePacketHeader();
  EXPECT_EQ(TestConnectionId(1),
            GetClientConnectionIdAsSender(header, Perspective::IS_SERVER));
  EXPECT_EQ(TestConnectionId(2),
            GetClientConnectionIdAsSender(header, Perspective::IS_CLIENT));
}

TEST_F(QuicPacketsTest, CopySerializedPacket) {
  std::string buffer(1000, 'a');
  SimpleBufferAllocator allocator;
  SerializedPacket packet(QuicPacketNumber(1), PACKET_1BYTE_PACKET_NUMBER,
                          buffer.data(), buffer.length(), /*has_ack=*/false,
                          /*has_stop_waiting=*/false);
  packet.retransmittable_frames.push_back(
      QuicFrame(new QuicWindowUpdateFrame()));
  packet.retransmittable_frames.push_back(QuicFrame(QuicStreamFrame()));

  QuicAckFrame ack_frame(InitAckFrame(1));
  packet.nonretransmittable_frames.push_back(QuicFrame(&ack_frame));
  packet.nonretransmittable_frames.push_back(QuicFrame(QuicPaddingFrame(-1)));

  std::unique_ptr<SerializedPacket> copy = QuicWrapUnique<SerializedPacket>(
      CopySerializedPacket(packet, &allocator, /*copy_buffer=*/true));
  EXPECT_EQ(quic::QuicPacketNumber(1), copy->packet_number);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER, copy->packet_number_length);
  ASSERT_EQ(2u, copy->retransmittable_frames.size());
  EXPECT_EQ(WINDOW_UPDATE_FRAME, copy->retransmittable_frames[0].type);
  EXPECT_EQ(STREAM_FRAME, copy->retransmittable_frames[1].type);

  ASSERT_EQ(2u, copy->nonretransmittable_frames.size());
  EXPECT_EQ(ACK_FRAME, copy->nonretransmittable_frames[0].type);
  EXPECT_EQ(PADDING_FRAME, copy->nonretransmittable_frames[1].type);
  EXPECT_EQ(1000u, copy->encrypted_length);
  quiche::test::CompareCharArraysWithHexError(
      "encrypted_buffer", copy->encrypted_buffer, copy->encrypted_length,
      packet.encrypted_buffer, packet.encrypted_length);

  std::unique_ptr<SerializedPacket> copy2 = QuicWrapUnique<SerializedPacket>(
      CopySerializedPacket(packet, &allocator, /*copy_buffer=*/false));
  EXPECT_EQ(packet.encrypted_buffer, copy2->encrypted_buffer);
  EXPECT_EQ(1000u, copy2->encrypted_length);
  ClearSerializedPacket(&packet);
  delete[] copy->encrypted_buffer;
  ClearSerializedPacket(copy.get());
  ClearSerializedPacket(copy2.get());
}

}  // namespace
}  // namespace test
}  // namespace quic
