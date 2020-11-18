// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_legacy_version_encapsulator.h"

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace test {
namespace {

class QuicLegacyVersionEncapsulatorTest
    : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  QuicLegacyVersionEncapsulatorTest()
      : version_(GetParam()),
        sni_("test.example.org"),
        server_connection_id_(TestConnectionId()),
        outer_max_packet_length_(kMaxOutgoingPacketSize),
        encapsulated_length_(0) {}

  void Encapsulate(const std::string& inner_packet) {
    encapsulated_length_ = QuicLegacyVersionEncapsulator::Encapsulate(
        sni_, inner_packet, server_connection_id_, QuicTime::Zero(),
        outer_max_packet_length_, outer_buffer_);
  }

  void CheckEncapsulation() {
    ASSERT_NE(encapsulated_length_, 0u);
    ASSERT_EQ(encapsulated_length_, outer_max_packet_length_);
    // Verify that the encapsulated packet parses as encapsulated.
    PacketHeaderFormat format = IETF_QUIC_LONG_HEADER_PACKET;
    QuicLongHeaderType long_packet_type;
    bool version_present;
    bool has_length_prefix;
    QuicVersionLabel version_label;
    ParsedQuicVersion parsed_version = ParsedQuicVersion::Unsupported();
    QuicConnectionId destination_connection_id, source_connection_id;
    bool retry_token_present;
    quiche::QuicheStringPiece retry_token;
    std::string detailed_error;
    const QuicErrorCode error = QuicFramer::ParsePublicHeaderDispatcher(
        QuicEncryptedPacket(outer_buffer_, encapsulated_length_),
        kQuicDefaultConnectionIdLength, &format, &long_packet_type,
        &version_present, &has_length_prefix, &version_label, &parsed_version,
        &destination_connection_id, &source_connection_id, &retry_token_present,
        &retry_token, &detailed_error);
    ASSERT_THAT(error, IsQuicNoError()) << detailed_error;
    EXPECT_EQ(format, GOOGLE_QUIC_PACKET);
    EXPECT_TRUE(version_present);
    EXPECT_FALSE(has_length_prefix);
    EXPECT_EQ(parsed_version, LegacyVersionForEncapsulation());
    EXPECT_EQ(destination_connection_id, server_connection_id_);
    EXPECT_EQ(source_connection_id, EmptyQuicConnectionId());
    EXPECT_FALSE(retry_token_present);
    EXPECT_TRUE(detailed_error.empty());
  }

  QuicByteCount Overhead() {
    return QuicLegacyVersionEncapsulator::GetMinimumOverhead(sni_);
  }

  ParsedQuicVersion version_;
  std::string sni_;
  QuicConnectionId server_connection_id_;
  QuicByteCount outer_max_packet_length_;
  char outer_buffer_[kMaxOutgoingPacketSize];
  QuicPacketLength encapsulated_length_;
};

INSTANTIATE_TEST_SUITE_P(QuicLegacyVersionEncapsulatorTests,
                         QuicLegacyVersionEncapsulatorTest,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicLegacyVersionEncapsulatorTest, Simple) {
  Encapsulate("TEST_INNER_PACKET");
  CheckEncapsulation();
}

TEST_P(QuicLegacyVersionEncapsulatorTest, TooBig) {
  std::string inner_packet(kMaxOutgoingPacketSize, '?');
  EXPECT_QUIC_BUG(Encapsulate(inner_packet), "Legacy Version Encapsulation");
  ASSERT_EQ(encapsulated_length_, 0u);
}

TEST_P(QuicLegacyVersionEncapsulatorTest, BarelyFits) {
  QuicByteCount inner_size = kMaxOutgoingPacketSize - Overhead();
  std::string inner_packet(inner_size, '?');
  Encapsulate(inner_packet);
  CheckEncapsulation();
}

TEST_P(QuicLegacyVersionEncapsulatorTest, DoesNotQuiteFit) {
  QuicByteCount inner_size = 1 + kMaxOutgoingPacketSize - Overhead();
  std::string inner_packet(inner_size, '?');
  EXPECT_QUIC_BUG(Encapsulate(inner_packet), "Legacy Version Encapsulation");
  ASSERT_EQ(encapsulated_length_, 0u);
}

}  // namespace
}  // namespace test
}  // namespace quic
