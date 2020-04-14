// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_client_push_promise_index.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_server_push_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_client_promised_info.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockQuicSpdyClientSession : public QuicSpdyClientSession {
 public:
  explicit MockQuicSpdyClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection,
      QuicClientPushPromiseIndex* push_promise_index)
      : QuicSpdyClientSession(DefaultQuicConfig(),
                              supported_versions,
                              connection,
                              QuicServerId("example.com", 443, false),
                              &crypto_config_,
                              push_promise_index),
        crypto_config_(crypto_test_utils::ProofVerifierForTesting()) {}
  MockQuicSpdyClientSession(const MockQuicSpdyClientSession&) = delete;
  MockQuicSpdyClientSession& operator=(const MockQuicSpdyClientSession&) =
      delete;
  ~MockQuicSpdyClientSession() override {}

  MOCK_METHOD1(CloseStream, void(QuicStreamId stream_id));

 private:
  QuicCryptoClientConfig crypto_config_;
};

class QuicClientPushPromiseIndexTest : public QuicTest {
 public:
  QuicClientPushPromiseIndexTest()
      : connection_(new StrictMock<MockQuicConnection>(&helper_,
                                                       &alarm_factory_,
                                                       Perspective::IS_CLIENT)),
        session_(connection_->supported_versions(), connection_, &index_),
        promised_(&session_,
                  GetNthServerInitiatedUnidirectionalStreamId(
                      connection_->transport_version(),
                      0),
                  url_) {
    request_[":path"] = "/bar";
    request_[":authority"] = "www.google.com";
    request_[":method"] = "GET";
    request_[":scheme"] = "https";
    url_ = SpdyServerPushUtils::GetPromisedUrlFromHeaders(request_);
  }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  MockQuicSpdyClientSession session_;
  QuicClientPushPromiseIndex index_;
  spdy::SpdyHeaderBlock request_;
  std::string url_;
  MockQuicClientPromisedInfo promised_;
  QuicClientPushPromiseIndex::TryHandle* handle_;
};

TEST_F(QuicClientPushPromiseIndexTest, TryRequestSuccess) {
  (*index_.promised_by_url())[url_] = &promised_;
  EXPECT_CALL(promised_, HandleClientRequest(_, _))
      .WillOnce(Return(QUIC_SUCCESS));
  EXPECT_EQ(index_.Try(request_, nullptr, &handle_), QUIC_SUCCESS);
}

TEST_F(QuicClientPushPromiseIndexTest, TryRequestPending) {
  (*index_.promised_by_url())[url_] = &promised_;
  EXPECT_CALL(promised_, HandleClientRequest(_, _))
      .WillOnce(Return(QUIC_PENDING));
  EXPECT_EQ(index_.Try(request_, nullptr, &handle_), QUIC_PENDING);
}

TEST_F(QuicClientPushPromiseIndexTest, TryRequestFailure) {
  (*index_.promised_by_url())[url_] = &promised_;
  EXPECT_CALL(promised_, HandleClientRequest(_, _))
      .WillOnce(Return(QUIC_FAILURE));
  EXPECT_EQ(index_.Try(request_, nullptr, &handle_), QUIC_FAILURE);
}

TEST_F(QuicClientPushPromiseIndexTest, TryNoPromise) {
  EXPECT_EQ(index_.Try(request_, nullptr, &handle_), QUIC_FAILURE);
}

TEST_F(QuicClientPushPromiseIndexTest, GetNoPromise) {
  EXPECT_EQ(index_.GetPromised(url_), nullptr);
}

TEST_F(QuicClientPushPromiseIndexTest, GetPromise) {
  (*index_.promised_by_url())[url_] = &promised_;
  EXPECT_EQ(index_.GetPromised(url_), &promised_);
}

}  // namespace
}  // namespace test
}  // namespace quic
