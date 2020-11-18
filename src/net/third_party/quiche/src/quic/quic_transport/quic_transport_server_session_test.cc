// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_server_session.h"

#include <cstddef>
#include <memory>
#include <string>

#include "url/gurl.h"
#include "url/origin.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_compressed_certs_cache.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_transport_test_tools.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace test {
namespace {

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::HasSubstr;
using testing::Return;
using testing::SaveArg;

constexpr char kTestOrigin[] = "https://test-origin.test";
constexpr char kTestOriginClientIndication[] =
    "\0\0"                      // key (0x0000, origin)
    "\0\x18"                    // length
    "https://test-origin.test"  // value
    "\0\x01"                    // key (0x0001, path)
    "\0\x05"                    // length
    "/test";                    // value
const url::Origin GetTestOrigin() {
  return url::Origin::Create(GURL(kTestOrigin));
}
const std::string GetTestOriginClientIndication() {
  return std::string(kTestOriginClientIndication,
                     sizeof(kTestOriginClientIndication) - 1);
}

ParsedQuicVersionVector GetVersions() {
  return {DefaultVersionForQuicTransport()};
}

class QuicTransportServerSessionTest : public QuicTest {
 public:
  QuicTransportServerSessionTest()
      : connection_(&helper_,
                    &alarm_factory_,
                    Perspective::IS_SERVER,
                    GetVersions()),
        crypto_config_(QuicCryptoServerConfig::TESTING,
                       QuicRandom::GetInstance(),
                       crypto_test_utils::ProofSourceForTesting(),
                       KeyExchangeSource::Default()),
        compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize) {
    QuicEnableVersion(DefaultVersionForQuicTransport());
    connection_.AdvanceTime(QuicTime::Delta::FromSeconds(100000));
    crypto_test_utils::SetupCryptoServerConfigForTest(
        helper_.GetClock(), helper_.GetRandomGenerator(), &crypto_config_);
    session_ = std::make_unique<QuicTransportServerSession>(
        &connection_, nullptr, DefaultQuicConfig(), GetVersions(),
        &crypto_config_, &compressed_certs_cache_, &visitor_);
    session_->Initialize();
    crypto_stream_ = session_->GetMutableCryptoStream();
    ON_CALL(visitor_, ProcessPath(_))
        .WillByDefault(DoAll(SaveArg<0>(&path_), Return(true)));
  }

  void Connect() {
    crypto_test_utils::FakeClientOptions options;
    options.only_tls_versions = true;
    crypto_test_utils::HandshakeWithFakeClient(
        &helper_, &alarm_factory_, &connection_, crypto_stream_,
        QuicServerId("test.example.com", 443), options, QuicTransportAlpn());
  }

  void ReceiveIndication(quiche::QuicheStringPiece indication) {
    QUIC_LOG(INFO) << "Receiving indication: "
                   << quiche::QuicheTextUtils::HexDump(indication);
    constexpr size_t kChunkSize = 1024;
    // Shard the indication, since some of the tests cause it to not fit into a
    // single frame.
    for (size_t i = 0; i < indication.size(); i += kChunkSize) {
      QuicStreamFrame frame(ClientIndicationStream(), /*fin=*/false, i,
                            indication.substr(i, i + kChunkSize));
      session_->OnStreamFrame(frame);
    }
    session_->OnStreamFrame(QuicStreamFrame(ClientIndicationStream(),
                                            /*fin=*/true, indication.size(),
                                            quiche::QuicheStringPiece()));
  }

  void ReceiveIndicationWithPath(quiche::QuicheStringPiece path) {
    constexpr char kTestOriginClientIndicationPrefix[] =
        "\0\0"                      // key (0x0000, origin)
        "\0\x18"                    // length
        "https://test-origin.test"  // value
        "\0\x01";                   // key (0x0001, path)
    std::string indication{kTestOriginClientIndicationPrefix,
                           sizeof(kTestOriginClientIndicationPrefix) - 1};
    indication.push_back(static_cast<char>(path.size() >> 8));
    indication.push_back(static_cast<char>(path.size() & 0xff));
    indication += std::string{path};
    ReceiveIndication(indication);
  }

 protected:
  MockAlarmFactory alarm_factory_;
  MockQuicConnectionHelper helper_;

  PacketSavingConnection connection_;
  QuicCryptoServerConfig crypto_config_;
  std::unique_ptr<QuicTransportServerSession> session_;
  QuicCompressedCertsCache compressed_certs_cache_;
  testing::NiceMock<MockServerVisitor> visitor_;
  QuicCryptoServerStreamBase* crypto_stream_;
  GURL path_;
};

TEST_F(QuicTransportServerSessionTest, SuccessfulHandshake) {
  Connect();

  url::Origin origin;
  EXPECT_CALL(visitor_, CheckOrigin(_))
      .WillOnce(DoAll(SaveArg<0>(&origin), Return(true)));
  ReceiveIndication(GetTestOriginClientIndication());
  EXPECT_TRUE(session_->IsSessionReady());
  EXPECT_EQ(origin, GetTestOrigin());
  EXPECT_EQ(path_.path(), "/test");
}

TEST_F(QuicTransportServerSessionTest, PiecewiseClientIndication) {
  Connect();
  size_t i = 0;
  for (; i < sizeof(kTestOriginClientIndication) - 2; i++) {
    QuicStreamFrame frame(
        ClientIndicationStream(), false, i,
        quiche::QuicheStringPiece(&kTestOriginClientIndication[i], 1));
    session_->OnStreamFrame(frame);
  }

  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  QuicStreamFrame last_frame(
      ClientIndicationStream(), true, i,
      quiche::QuicheStringPiece(&kTestOriginClientIndication[i], 1));
  session_->OnStreamFrame(last_frame);
  EXPECT_TRUE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, OriginRejected) {
  Connect();
  EXPECT_CALL(connection_,
              CloseConnection(_, HasSubstr("Origin check failed"), _));
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(false));
  ReceiveIndication(GetTestOriginClientIndication());
  EXPECT_FALSE(session_->IsSessionReady());
}

std::string MakeUnknownField(quiche::QuicheStringPiece payload) {
  std::string buffer;
  buffer.resize(payload.size() + 4);
  QuicDataWriter writer(buffer.size(), &buffer[0]);
  EXPECT_TRUE(writer.WriteUInt16(0xffff));
  EXPECT_TRUE(writer.WriteUInt16(payload.size()));
  EXPECT_TRUE(writer.WriteStringPiece(payload));
  EXPECT_EQ(writer.remaining(), 0u);
  return buffer;
}

TEST_F(QuicTransportServerSessionTest, SkipUnusedFields) {
  Connect();
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  ReceiveIndication(GetTestOriginClientIndication() +
                    MakeUnknownField("foobar"));
  EXPECT_TRUE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, SkipLongUnusedFields) {
  const size_t bytes =
      ClientIndicationMaxSize() - GetTestOriginClientIndication().size() - 4;
  Connect();
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  ReceiveIndication(GetTestOriginClientIndication() +
                    MakeUnknownField(std::string(bytes, 'a')));
  EXPECT_TRUE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, ClientIndicationTooLong) {
  Connect();
  EXPECT_CALL(
      connection_,
      CloseConnection(_, HasSubstr("Client indication size exceeds"), _))
      .Times(AnyNumber());
  ReceiveIndication(GetTestOriginClientIndication() +
                    MakeUnknownField(std::string(65534, 'a')));
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, NoOrigin) {
  Connect();
  EXPECT_CALL(connection_, CloseConnection(_, HasSubstr("No origin"), _));
  ReceiveIndication(MakeUnknownField("foobar"));
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, EmptyClientIndication) {
  Connect();
  EXPECT_CALL(connection_, CloseConnection(_, HasSubstr("No origin"), _));
  ReceiveIndication("");
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, MalformedIndicationHeader) {
  Connect();
  EXPECT_CALL(connection_,
              CloseConnection(_, HasSubstr("Expected 16-bit key"), _));
  ReceiveIndication("\xff");
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, FieldTooShort) {
  Connect();
  EXPECT_CALL(
      connection_,
      CloseConnection(_, HasSubstr("Failed to read value for key 257"), _));
  ReceiveIndication("\x01\x01\x01\x01");
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, InvalidOrigin) {
  const std::string kEmptyOriginIndication(4, '\0');
  Connect();
  EXPECT_CALL(
      connection_,
      CloseConnection(_, HasSubstr("Unable to parse the specified origin"), _));
  ReceiveIndication(kEmptyOriginIndication);
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, PathWithQuery) {
  Connect();
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  ReceiveIndicationWithPath("/test?foo=bar");
  EXPECT_TRUE(session_->IsSessionReady());
  EXPECT_EQ(path_.path(), "/test");
  EXPECT_EQ(path_.query(), "foo=bar");
}

TEST_F(QuicTransportServerSessionTest, PathNormalization) {
  Connect();
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  ReceiveIndicationWithPath("/foo/../bar");
  EXPECT_TRUE(session_->IsSessionReady());
  EXPECT_EQ(path_.path(), "/bar");
}

TEST_F(QuicTransportServerSessionTest, EmptyPath) {
  Connect();
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  EXPECT_CALL(connection_,
              CloseConnection(_, HasSubstr("Path must begin with a '/'"), _));
  ReceiveIndicationWithPath("");
  EXPECT_FALSE(session_->IsSessionReady());
}

TEST_F(QuicTransportServerSessionTest, UnprefixedPath) {
  Connect();
  EXPECT_CALL(visitor_, CheckOrigin(_)).WillOnce(Return(true));
  EXPECT_CALL(connection_,
              CloseConnection(_, HasSubstr("Path must begin with a '/'"), _));
  ReceiveIndicationWithPath("test");
  EXPECT_FALSE(session_->IsSessionReady());
}

}  // namespace
}  // namespace test
}  // namespace quic
