// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packets.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

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

}  // namespace
}  // namespace test
}  // namespace quic
