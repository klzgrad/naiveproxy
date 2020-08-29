// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_client_session.h"

#include <memory>
#include <utility>

#include "url/gurl.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_transport_test_tools.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {
namespace test {
namespace {

using testing::_;
using testing::ElementsAre;
using testing::Eq;

const char* kTestOrigin = "https://test-origin.test";
url::Origin GetTestOrigin() {
  GURL origin_url(kTestOrigin);
  return url::Origin::Create(origin_url);
}

ParsedQuicVersionVector GetVersions() {
  return {DefaultVersionForQuicTransport()};
}

std::string DataInStream(QuicStream* stream) {
  QuicStreamSendBuffer& send_buffer = QuicStreamPeer::SendBuffer(stream);
  std::string result;
  result.resize(send_buffer.stream_offset());
  QuicDataWriter writer(result.size(), &result[0]);
  EXPECT_TRUE(
      send_buffer.WriteStreamData(0, send_buffer.stream_offset(), &writer));
  return result;
}

class QuicTransportClientSessionTest : public QuicTest {
 protected:
  QuicTransportClientSessionTest()
      : connection_(&helper_,
                    &alarm_factory_,
                    Perspective::IS_CLIENT,
                    GetVersions()),
        crypto_config_(crypto_test_utils::ProofVerifierForTesting()) {
    QuicEnableVersion(DefaultVersionForQuicTransport());
    CreateSession(GetTestOrigin(), "");
  }

  void CreateSession(url::Origin origin, std::string url_suffix) {
    session_ = std::make_unique<QuicTransportClientSession>(
        &connection_, nullptr, DefaultQuicConfig(), GetVersions(),
        GURL("quic-transport://test.example.com:50000" + url_suffix),
        &crypto_config_, origin, &visitor_);
    session_->Initialize();
    crypto_stream_ = static_cast<QuicCryptoClientStream*>(
        session_->GetMutableCryptoStream());
  }

  void Connect() {
    session_->CryptoConnect();
    QuicConfig server_config = DefaultQuicConfig();
    std::unique_ptr<QuicCryptoServerConfig> crypto_config(
        crypto_test_utils::CryptoServerConfigForTesting());
    crypto_test_utils::HandshakeWithFakeServer(
        &server_config, crypto_config.get(), &helper_, &alarm_factory_,
        &connection_, crypto_stream_, QuicTransportAlpn());
  }

  MockAlarmFactory alarm_factory_;
  MockQuicConnectionHelper helper_;

  PacketSavingConnection connection_;
  QuicCryptoClientConfig crypto_config_;
  MockClientVisitor visitor_;
  std::unique_ptr<QuicTransportClientSession> session_;
  QuicCryptoClientStream* crypto_stream_;
};

TEST_F(QuicTransportClientSessionTest, HasValidAlpn) {
  EXPECT_THAT(session_->GetAlpnsToOffer(), ElementsAre(QuicTransportAlpn()));
}

TEST_F(QuicTransportClientSessionTest, SuccessfulConnection) {
  constexpr char kTestOriginClientIndication[] =
      "\0\0"                      // key (0x0000, origin)
      "\0\x18"                    // length
      "https://test-origin.test"  // value
      "\0\x01"                    // key (0x0001, path)
      "\0\x01"                    // length
      "/";                        // value

  EXPECT_CALL(visitor_, OnSessionReady());
  Connect();
  EXPECT_TRUE(session_->IsSessionReady());

  QuicStream* client_indication_stream =
      QuicSessionPeer::zombie_streams(session_.get())[ClientIndicationStream()]
          .get();
  ASSERT_TRUE(client_indication_stream != nullptr);
  const std::string client_indication = DataInStream(client_indication_stream);
  const std::string expected_client_indication{
      kTestOriginClientIndication,
      QUICHE_ARRAYSIZE(kTestOriginClientIndication) - 1};
  EXPECT_EQ(client_indication, expected_client_indication);
}

TEST_F(QuicTransportClientSessionTest, SuccessfulConnectionWithPath) {
  constexpr char kSuffix[] = "/foo/bar?hello=world#not-sent";
  constexpr char kTestOriginClientIndication[] =
      "\0\0"                      // key (0x0000, origin)
      "\0\x18"                    // length
      "https://test-origin.test"  // value
      "\0\x01"                    // key (0x0001, path)
      "\0\x14"                    // length
      "/foo/bar?hello=world";     // value

  CreateSession(GetTestOrigin(), kSuffix);
  Connect();
  EXPECT_TRUE(session_->IsSessionReady());

  QuicStream* client_indication_stream =
      QuicSessionPeer::zombie_streams(session_.get())[ClientIndicationStream()]
          .get();
  ASSERT_TRUE(client_indication_stream != nullptr);
  const std::string client_indication = DataInStream(client_indication_stream);
  const std::string expected_client_indication{
      kTestOriginClientIndication,
      QUICHE_ARRAYSIZE(kTestOriginClientIndication) - 1};
  EXPECT_EQ(client_indication, expected_client_indication);
}

TEST_F(QuicTransportClientSessionTest, OriginTooLong) {
  std::string long_string(68000, 'a');
  GURL bad_origin_url{"https://" + long_string + ".example/"};
  EXPECT_TRUE(bad_origin_url.is_valid());
  CreateSession(url::Origin::Create(bad_origin_url), "");

  EXPECT_QUIC_BUG(Connect(), "Client origin too long");
}

TEST_F(QuicTransportClientSessionTest, ReceiveNewStreams) {
  Connect();
  ASSERT_TRUE(session_->IsSessionReady());
  ASSERT_TRUE(session_->AcceptIncomingUnidirectionalStream() == nullptr);

  const QuicStreamId id = GetNthServerInitiatedUnidirectionalStreamId(
      session_->transport_version(), 0);
  QuicStreamFrame frame(id, /*fin=*/false, /*offset=*/0, "test");
  EXPECT_CALL(visitor_, OnIncomingUnidirectionalStreamAvailable()).Times(1);
  session_->OnStreamFrame(frame);

  QuicTransportStream* stream = session_->AcceptIncomingUnidirectionalStream();
  ASSERT_TRUE(stream != nullptr);
  EXPECT_EQ(stream->ReadableBytes(), 4u);
  EXPECT_EQ(stream->id(), id);
}

TEST_F(QuicTransportClientSessionTest, ReceiveDatagram) {
  EXPECT_CALL(visitor_, OnDatagramReceived(Eq("test")));
  session_->OnMessageReceived("test");
}

}  // namespace
}  // namespace test
}  // namespace quic
