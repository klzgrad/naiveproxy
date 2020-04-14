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
  }

  QuicConnectionId connection_id() const { return connection_id_; }
  QuicTransportVersion transport_version() const { return version_; }
  const std::string& chlo() const { return chlo_; }

 private:
  QuicConnectionId connection_id_;
  QuicTransportVersion version_;
  std::string chlo_;
};

class ChloExtractorTest : public QuicTest {
 public:
  ChloExtractorTest() {
    header_.destination_connection_id = TestConnectionId();
    header_.destination_connection_id_included = CONNECTION_ID_PRESENT;
    header_.version_flag = true;
    header_.version = AllSupportedVersions().front();
    header_.reset_flag = false;
    header_.packet_number_length = PACKET_4BYTE_PACKET_NUMBER;
    header_.packet_number = QuicPacketNumber(1);
    if (QuicVersionHasLongHeaderLengths(header_.version.transport_version)) {
      header_.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
      header_.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
    }
  }

  void MakePacket(ParsedQuicVersion version,
                  quiche::QuicheStringPiece data,
                  bool munge_offset,
                  bool munge_stream_id) {
    QuicFrames frames;
    size_t offset = 0;
    if (munge_offset) {
      offset++;
    }
    QuicFramer framer(SupportedVersions(header_.version), QuicTime::Zero(),
                      Perspective::IS_CLIENT, kQuicDefaultConnectionIdLength);
    framer.SetInitialObfuscators(TestConnectionId());
    if (!QuicVersionUsesCryptoFrames(version.transport_version) ||
        munge_stream_id) {
      QuicStreamId stream_id =
          QuicUtils::GetCryptoStreamId(version.transport_version);
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
        BuildUnsizedDataPacket(&framer, header_, frames));
    EXPECT_TRUE(packet != nullptr);
    size_t encrypted_length =
        framer.EncryptPayload(ENCRYPTION_INITIAL, header_.packet_number,
                              *packet, buffer_, QUICHE_ARRAYSIZE(buffer_));
    ASSERT_NE(0u, encrypted_length);
    packet_ = std::make_unique<QuicEncryptedPacket>(buffer_, encrypted_length);
    EXPECT_TRUE(packet_ != nullptr);
    DeleteFrames(&frames);
  }

 protected:
  TestDelegate delegate_;
  QuicPacketHeader header_;
  std::unique_ptr<QuicEncryptedPacket> packet_;
  char buffer_[kMaxOutgoingPacketSize];
};

TEST_F(ChloExtractorTest, FindsValidChlo) {
  CryptoHandshakeMessage client_hello;
  client_hello.set_tag(kCHLO);

  std::string client_hello_str(client_hello.GetSerialized().AsStringPiece());
  // Construct a CHLO with each supported version
  for (ParsedQuicVersion version : AllSupportedVersions()) {
    SCOPED_TRACE(version);
    header_.version = version;
    if (QuicVersionHasLongHeaderLengths(version.transport_version) &&
        header_.version_flag) {
      header_.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
      header_.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
    } else {
      header_.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_0;
      header_.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_0;
    }
    MakePacket(version, client_hello_str, /*munge_offset*/ false,
               /*munge_stream_id*/ false);
    EXPECT_TRUE(ChloExtractor::Extract(*packet_, version, {}, &delegate_,
                                       kQuicDefaultConnectionIdLength))
        << ParsedQuicVersionToString(version);
    EXPECT_EQ(version.transport_version, delegate_.transport_version());
    EXPECT_EQ(header_.destination_connection_id, delegate_.connection_id());
    EXPECT_EQ(client_hello.DebugString(), delegate_.chlo())
        << ParsedQuicVersionToString(version);
  }
}

TEST_F(ChloExtractorTest, DoesNotFindValidChloOnWrongStream) {
  ParsedQuicVersion version = AllSupportedVersions()[0];
  if (QuicVersionUsesCryptoFrames(version.transport_version)) {
    return;
  }
  CryptoHandshakeMessage client_hello;
  client_hello.set_tag(kCHLO);

  std::string client_hello_str(client_hello.GetSerialized().AsStringPiece());
  MakePacket(version, client_hello_str,
             /*munge_offset*/ false, /*munge_stream_id*/ true);
  EXPECT_FALSE(ChloExtractor::Extract(*packet_, version, {}, &delegate_,
                                      kQuicDefaultConnectionIdLength));
}

TEST_F(ChloExtractorTest, DoesNotFindValidChloOnWrongOffset) {
  ParsedQuicVersion version = AllSupportedVersions()[0];
  CryptoHandshakeMessage client_hello;
  client_hello.set_tag(kCHLO);

  std::string client_hello_str(client_hello.GetSerialized().AsStringPiece());
  MakePacket(version, client_hello_str, /*munge_offset*/ true,
             /*munge_stream_id*/ false);
  EXPECT_FALSE(ChloExtractor::Extract(*packet_, version, {}, &delegate_,
                                      kQuicDefaultConnectionIdLength));
}

TEST_F(ChloExtractorTest, DoesNotFindInvalidChlo) {
  ParsedQuicVersion version = AllSupportedVersions()[0];
  if (QuicVersionUsesCryptoFrames(version.transport_version)) {
    return;
  }
  MakePacket(version, "foo", /*munge_offset*/ false,
             /*munge_stream_id*/ true);
  EXPECT_FALSE(ChloExtractor::Extract(*packet_, version, {}, &delegate_,
                                      kQuicDefaultConnectionIdLength));
}

}  // namespace
}  // namespace test
}  // namespace quic
