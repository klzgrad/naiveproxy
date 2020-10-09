// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/chlo_extractor.h"

#include <memory>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/first_flight.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

class TestDelegate : public ChloExtractor::Delegate {
 public:
  TestDelegate() = default;
  ~TestDelegate() override = default;

  // ChloExtractor::Delegate implementation
  void OnChlo(QuicTransportVersion version,
              QuicConnectionId connection_id,
              const CryptoHandshakeMessage& chlo) override {
    version_ = version;
    connection_id_ = connection_id;
    chlo_ = chlo.DebugString();
    quiche::QuicheStringPiece alpn_value;
    if (chlo.GetStringPiece(kALPN, &alpn_value)) {
      alpn_ = std::string(alpn_value);
    }
  }

  QuicConnectionId connection_id() const { return connection_id_; }
  QuicTransportVersion transport_version() const { return version_; }
  const std::string& chlo() const { return chlo_; }
  const std::string& alpn() const { return alpn_; }

 private:
  QuicConnectionId connection_id_;
  QuicTransportVersion version_;
  std::string chlo_;
  std::string alpn_;
};

class ChloExtractorTest : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  ChloExtractorTest() : version_(GetParam()) {}

  void MakePacket(quiche::QuicheStringPiece data,
                  bool munge_offset,
                  bool munge_stream_id) {
    QuicPacketHeader header;
    header.destination_connection_id = TestConnectionId();
    header.destination_connection_id_included = CONNECTION_ID_PRESENT;
    header.version_flag = true;
    header.version = version_;
    header.reset_flag = false;
    header.packet_number_length = PACKET_4BYTE_PACKET_NUMBER;
    header.packet_number = QuicPacketNumber(1);
    if (version_.HasLongHeaderLengths()) {
      header.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
      header.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
    }
    QuicFrames frames;
    size_t offset = 0;
    if (munge_offset) {
      offset++;
    }
    QuicFramer framer(SupportedVersions(version_), QuicTime::Zero(),
                      Perspective::IS_CLIENT, kQuicDefaultConnectionIdLength);
    framer.SetInitialObfuscators(TestConnectionId());
    if (!version_.UsesCryptoFrames() || munge_stream_id) {
      QuicStreamId stream_id =
          QuicUtils::GetCryptoStreamId(version_.transport_version);
      if (munge_stream_id) {
        stream_id++;
      }
      frames.push_back(
          QuicFrame(QuicStreamFrame(stream_id, false, offset, data)));
    } else {
      frames.push_back(
          QuicFrame(new QuicCryptoFrame(ENCRYPTION_INITIAL, offset, data)));
    }
    std::unique_ptr<QuicPacket> packet(
        BuildUnsizedDataPacket(&framer, header, frames));
    EXPECT_TRUE(packet != nullptr);
    size_t encrypted_length =
        framer.EncryptPayload(ENCRYPTION_INITIAL, header.packet_number, *packet,
                              buffer_, QUICHE_ARRAYSIZE(buffer_));
    ASSERT_NE(0u, encrypted_length);
    packet_ = std::make_unique<QuicEncryptedPacket>(buffer_, encrypted_length);
    EXPECT_TRUE(packet_ != nullptr);
    DeleteFrames(&frames);
  }

 protected:
  ParsedQuicVersion version_;
  TestDelegate delegate_;
  std::unique_ptr<QuicEncryptedPacket> packet_;
  char buffer_[kMaxOutgoingPacketSize];
};

INSTANTIATE_TEST_SUITE_P(
    ChloExtractorTests,
    ChloExtractorTest,
    ::testing::ValuesIn(AllSupportedVersionsWithQuicCrypto()),
    ::testing::PrintToStringParamName());

TEST_P(ChloExtractorTest, FindsValidChlo) {
  CryptoHandshakeMessage client_hello;
  client_hello.set_tag(kCHLO);

  std::string client_hello_str(client_hello.GetSerialized().AsStringPiece());

  MakePacket(client_hello_str, /*munge_offset=*/false,
             /*munge_stream_id=*/false);
  EXPECT_TRUE(ChloExtractor::Extract(*packet_, version_, {}, &delegate_,
                                     kQuicDefaultConnectionIdLength));
  EXPECT_EQ(version_.transport_version, delegate_.transport_version());
  EXPECT_EQ(TestConnectionId(), delegate_.connection_id());
  EXPECT_EQ(client_hello.DebugString(), delegate_.chlo());
}

TEST_P(ChloExtractorTest, DoesNotFindValidChloOnWrongStream) {
  if (version_.UsesCryptoFrames()) {
    // When crypto frames are in use we do not use stream frames.
    return;
  }
  CryptoHandshakeMessage client_hello;
  client_hello.set_tag(kCHLO);

  std::string client_hello_str(client_hello.GetSerialized().AsStringPiece());
  MakePacket(client_hello_str,
             /*munge_offset=*/false, /*munge_stream_id=*/true);
  EXPECT_FALSE(ChloExtractor::Extract(*packet_, version_, {}, &delegate_,
                                      kQuicDefaultConnectionIdLength));
}

TEST_P(ChloExtractorTest, DoesNotFindValidChloOnWrongOffset) {
  CryptoHandshakeMessage client_hello;
  client_hello.set_tag(kCHLO);

  std::string client_hello_str(client_hello.GetSerialized().AsStringPiece());
  MakePacket(client_hello_str, /*munge_offset=*/true,
             /*munge_stream_id=*/false);
  EXPECT_FALSE(ChloExtractor::Extract(*packet_, version_, {}, &delegate_,
                                      kQuicDefaultConnectionIdLength));
}

TEST_P(ChloExtractorTest, DoesNotFindInvalidChlo) {
  MakePacket("foo", /*munge_offset=*/false,
             /*munge_stream_id=*/false);
  EXPECT_FALSE(ChloExtractor::Extract(*packet_, version_, {}, &delegate_,
                                      kQuicDefaultConnectionIdLength));
}

TEST_P(ChloExtractorTest, FirstFlight) {
  std::vector<std::unique_ptr<QuicReceivedPacket>> packets =
      GetFirstFlightOfPackets(version_);
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_TRUE(ChloExtractor::Extract(*packets[0], version_, {}, &delegate_,
                                     kQuicDefaultConnectionIdLength));
  EXPECT_EQ(version_.transport_version, delegate_.transport_version());
  EXPECT_EQ(TestConnectionId(), delegate_.connection_id());
  EXPECT_EQ(AlpnForVersion(version_), delegate_.alpn());
}

}  // namespace
}  // namespace test
}  // namespace quic
