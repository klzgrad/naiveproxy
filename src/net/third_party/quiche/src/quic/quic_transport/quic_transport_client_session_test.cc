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
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

using testing::_;
using testing::ElementsAre;

const char* kTestOrigin = "https://test-origin.test";
constexpr char kTestOriginClientIndication[] =
    "\0\0\0\x18https://test-origin.test";
url::Origin GetTestOrigin() {
  GURL origin_url(kTestOrigin);
  return url::Origin::Create(origin_url);
}

ParsedQuicVersionVector GetVersions() {
  return {ParsedQuicVersion{PROTOCOL_TLS1_3, QUIC_VERSION_99}};
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

class TestClientSession : public QuicTransportClientSession {
 public:
  using QuicTransportClientSession::QuicTransportClientSession;

  class Stream : public QuicStream {
   public:
    using QuicStream::QuicStream;
    void OnDataAvailable() override {}
  };

  QuicStream* CreateIncomingStream(QuicStreamId id) override {
    auto stream = std::make_unique<Stream>(
        id, this, /*is_static=*/false,
        QuicUtils::GetStreamType(id, connection()->perspective(),
                                 /*peer_initiated=*/true));
    QuicStream* result = stream.get();
    ActivateStream(std::move(stream));
    return result;
  }

  QuicStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    QUIC_NOTREACHED();
    return nullptr;
  }
};

class QuicTransportClientSessionTest : public QuicTest {
 protected:
  QuicTransportClientSessionTest()
      : connection_(&helper_,
                    &alarm_factory_,
                    Perspective::IS_CLIENT,
                    GetVersions()),
        server_id_("test.example.com", 443),
        crypto_config_(crypto_test_utils::ProofVerifierForTesting()) {
    SetQuicReloadableFlag(quic_supports_tls_handshake, true);
    CreateSession(GetTestOrigin());
  }

  void CreateSession(url::Origin origin) {
    session_ = std::make_unique<TestClientSession>(
        &connection_, nullptr, DefaultQuicConfig(), GetVersions(), server_id_,
        &crypto_config_, origin);
    session_->Initialize();
    crypto_stream_ = static_cast<QuicCryptoClientStream*>(
        session_->GetMutableCryptoStream());
  }

  void Connect() {
    session_->CryptoConnect();
    QuicConfig server_config = DefaultQuicConfig();
    crypto_test_utils::HandshakeWithFakeServer(
        &server_config, &helper_, &alarm_factory_, &connection_, crypto_stream_,
        QuicTransportAlpn());
  }

  MockAlarmFactory alarm_factory_;
  MockQuicConnectionHelper helper_;

  PacketSavingConnection connection_;
  QuicServerId server_id_;
  QuicCryptoClientConfig crypto_config_;
  std::unique_ptr<TestClientSession> session_;
  QuicCryptoClientStream* crypto_stream_;
};

TEST_F(QuicTransportClientSessionTest, HasValidAlpn) {
  EXPECT_THAT(session_->GetAlpnsToOffer(), ElementsAre(QuicTransportAlpn()));
}

TEST_F(QuicTransportClientSessionTest, SuccessfulConnection) {
  Connect();
  EXPECT_TRUE(session_->IsSessionReady());

  QuicStream* client_indication_stream =
      QuicSessionPeer::zombie_streams(session_.get())[ClientIndicationStream()]
          .get();
  ASSERT_TRUE(client_indication_stream != nullptr);
  const std::string client_indication = DataInStream(client_indication_stream);
  const std::string expected_client_indication{
      kTestOriginClientIndication,
      QUIC_ARRAYSIZE(kTestOriginClientIndication) - 1};
  EXPECT_EQ(client_indication, expected_client_indication);
}

TEST_F(QuicTransportClientSessionTest, OriginTooLong) {
  std::string long_string(68000, 'a');
  GURL bad_origin_url{"https://" + long_string + ".example/"};
  EXPECT_TRUE(bad_origin_url.is_valid());
  CreateSession(url::Origin::Create(bad_origin_url));

  EXPECT_QUIC_BUG(Connect(), "Client origin too long");
}

}  // namespace
}  // namespace test
}  // namespace quic
