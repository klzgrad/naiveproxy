// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/tls_chlo_extractor.h"
#include <memory>

#include "quic/core/http/quic_spdy_client_session.h"
#include "quic/core/quic_connection.h"
#include "quic/core/quic_packet_writer_wrapper.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/crypto_test_utils.h"
#include "quic/test_tools/first_flight.h"
#include "quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

class TlsChloExtractorTest : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  TlsChloExtractorTest() : version_(GetParam()) {}

  void Initialize() { packets_ = GetFirstFlightOfPackets(version_, config_); }

  void IngestPackets() {
    for (const std::unique_ptr<QuicReceivedPacket>& packet : packets_) {
      ReceivedPacketInfo packet_info(
          QuicSocketAddress(TestPeerIPAddress(), kTestPort),
          QuicSocketAddress(TestPeerIPAddress(), kTestPort), *packet);
      std::string detailed_error;
      bool retry_token_present;
      absl::string_view retry_token;
      const QuicErrorCode error = QuicFramer::ParsePublicHeaderDispatcher(
          *packet, /*expected_destination_connection_id_length=*/0,
          &packet_info.form, &packet_info.long_packet_type,
          &packet_info.version_flag, &packet_info.use_length_prefix,
          &packet_info.version_label, &packet_info.version,
          &packet_info.destination_connection_id,
          &packet_info.source_connection_id, &retry_token_present, &retry_token,
          &detailed_error);
      ASSERT_THAT(error, IsQuicNoError()) << detailed_error;
      tls_chlo_extractor_.IngestPacket(packet_info.version, packet_info.packet);
    }
    packets_.clear();
  }

  void ValidateChloDetails() {
    EXPECT_TRUE(tls_chlo_extractor_.HasParsedFullChlo());
    std::vector<std::string> alpns = tls_chlo_extractor_.alpns();
    ASSERT_EQ(alpns.size(), 1u);
    EXPECT_EQ(alpns[0], AlpnForVersion(version_));
    EXPECT_EQ(tls_chlo_extractor_.server_name(), TestHostname());
  }

  void IncreaseSizeOfChlo() {
    // Add a 2000-byte custom parameter to increase the length of the CHLO.
    constexpr auto kCustomParameterId =
        static_cast<TransportParameters::TransportParameterId>(0xff33);
    std::string kCustomParameterValue(2000, '-');
    config_.custom_transport_parameters_to_send()[kCustomParameterId] =
        kCustomParameterValue;
  }

  ParsedQuicVersion version_;
  TlsChloExtractor tls_chlo_extractor_;
  QuicConfig config_;
  std::vector<std::unique_ptr<QuicReceivedPacket>> packets_;
};

INSTANTIATE_TEST_SUITE_P(TlsChloExtractorTests,
                         TlsChloExtractorTest,
                         ::testing::ValuesIn(AllSupportedVersionsWithTls()),
                         ::testing::PrintToStringParamName());

TEST_P(TlsChloExtractorTest, Simple) {
  Initialize();
  EXPECT_EQ(packets_.size(), 1u);
  IngestPackets();
  ValidateChloDetails();
  EXPECT_EQ(tls_chlo_extractor_.state(),
            TlsChloExtractor::State::kParsedFullSinglePacketChlo);
}

TEST_P(TlsChloExtractorTest, MultiPacket) {
  IncreaseSizeOfChlo();
  Initialize();
  EXPECT_EQ(packets_.size(), 2u);
  IngestPackets();
  ValidateChloDetails();
  EXPECT_EQ(tls_chlo_extractor_.state(),
            TlsChloExtractor::State::kParsedFullMultiPacketChlo);
}

TEST_P(TlsChloExtractorTest, MultiPacketReordered) {
  IncreaseSizeOfChlo();
  Initialize();
  ASSERT_EQ(packets_.size(), 2u);
  // Artifically reorder both packets.
  std::swap(packets_[0], packets_[1]);
  IngestPackets();
  ValidateChloDetails();
  EXPECT_EQ(tls_chlo_extractor_.state(),
            TlsChloExtractor::State::kParsedFullMultiPacketChlo);
}

TEST_P(TlsChloExtractorTest, MoveAssignment) {
  Initialize();
  EXPECT_EQ(packets_.size(), 1u);
  TlsChloExtractor other_extractor;
  tls_chlo_extractor_ = std::move(other_extractor);
  IngestPackets();
  ValidateChloDetails();
  EXPECT_EQ(tls_chlo_extractor_.state(),
            TlsChloExtractor::State::kParsedFullSinglePacketChlo);
}

TEST_P(TlsChloExtractorTest, MoveAssignmentBetweenPackets) {
  IncreaseSizeOfChlo();
  Initialize();
  ASSERT_EQ(packets_.size(), 2u);
  TlsChloExtractor other_extractor;

  // Have |other_extractor| parse the first packet.
  ReceivedPacketInfo packet_info(
      QuicSocketAddress(TestPeerIPAddress(), kTestPort),
      QuicSocketAddress(TestPeerIPAddress(), kTestPort), *packets_[0]);
  std::string detailed_error;
  bool retry_token_present;
  absl::string_view retry_token;
  const QuicErrorCode error = QuicFramer::ParsePublicHeaderDispatcher(
      *packets_[0], /*expected_destination_connection_id_length=*/0,
      &packet_info.form, &packet_info.long_packet_type,
      &packet_info.version_flag, &packet_info.use_length_prefix,
      &packet_info.version_label, &packet_info.version,
      &packet_info.destination_connection_id, &packet_info.source_connection_id,
      &retry_token_present, &retry_token, &detailed_error);
  ASSERT_THAT(error, IsQuicNoError()) << detailed_error;
  other_extractor.IngestPacket(packet_info.version, packet_info.packet);
  // Remove the first packet from the list.
  packets_.erase(packets_.begin());
  EXPECT_EQ(packets_.size(), 1u);

  // Move the extractor.
  tls_chlo_extractor_ = std::move(other_extractor);

  // Have |tls_chlo_extractor_| parse the second packet.
  IngestPackets();

  ValidateChloDetails();
  EXPECT_EQ(tls_chlo_extractor_.state(),
            TlsChloExtractor::State::kParsedFullMultiPacketChlo);
}

}  // namespace
}  // namespace test
}  // namespace quic
