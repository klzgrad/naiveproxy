// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/http/quic_spdy_client_stream.h"

#include <memory>

#include "base/macros.h"
#include "net/third_party/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quic/core/http/spdy_utils.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"

using spdy::SpdyHeaderBlock;
using testing::_;
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
        crypto_config_(crypto_test_utils::ProofVerifierForTesting(),
                       TlsClientHandshaker::CreateSslCtx()) {}
  MockQuicSpdyClientSession(const MockQuicSpdyClientSession&) = delete;
  MockQuicSpdyClientSession& operator=(const MockQuicSpdyClientSession&) =
      delete;
  ~MockQuicSpdyClientSession() override = default;

  MOCK_METHOD1(CloseStream, void(QuicStreamId stream_id));

 private:
  QuicCryptoClientConfig crypto_config_;
};

class QuicSpdyClientStreamTest : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  class StreamVisitor;

  QuicSpdyClientStreamTest()
      : connection_(
            new StrictMock<MockQuicConnection>(&helper_,
                                               &alarm_factory_,
                                               Perspective::IS_CLIENT,
                                               SupportedVersions(GetParam()))),
        session_(connection_->supported_versions(),
                 connection_,
                 &push_promise_index_),
        body_("hello world") {
    session_.Initialize();

    headers_[":status"] = "200";
    headers_["content-length"] = "11";

    stream_ = QuicMakeUnique<QuicSpdyClientStream>(
        GetNthClientInitiatedBidirectionalStreamId(
            connection_->transport_version(), 0),
        &session_, BIDIRECTIONAL);
    stream_visitor_ = QuicMakeUnique<StreamVisitor>();
    stream_->set_visitor(stream_visitor_.get());
  }

  class StreamVisitor : public QuicSpdyClientStream::Visitor {
    void OnClose(QuicSpdyStream* stream) override {
      QUIC_DVLOG(1) << "stream " << stream->id();
    }
  };

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  QuicClientPushPromiseIndex push_promise_index_;

  MockQuicSpdyClientSession session_;
  std::unique_ptr<QuicSpdyClientStream> stream_;
  std::unique_ptr<StreamVisitor> stream_visitor_;
  SpdyHeaderBlock headers_;
  QuicString body_;
  HttpEncoder encoder_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSpdyClientStreamTest,
                         ::testing::ValuesIn(AllSupportedVersions()));

TEST_P(QuicSpdyClientStreamTest, TestReceivingIllegalResponseStatusCode) {
  headers_[":status"] = "200 ok";

  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(stream_->id(), QUIC_BAD_APPLICATION_PAYLOAD));
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);
  EXPECT_EQ(QUIC_BAD_APPLICATION_PAYLOAD, stream_->stream_error());
}

TEST_P(QuicSpdyClientStreamTest, TestFraming) {
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(body_.length(), &buffer);
  QuicString header = QuicString(buffer.get(), header_length);
  QuicString data = VersionHasDataFrameHeader(connection_->transport_version())
                        ? header + body_
                        : body_;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  EXPECT_EQ("200", stream_->response_headers().find(":status")->second);
  EXPECT_EQ(200, stream_->response_code());
  EXPECT_EQ(body_, stream_->data());
}

TEST_P(QuicSpdyClientStreamTest, TestFraming100Continue) {
  headers_[":status"] = "100";
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, body_));
  EXPECT_EQ("100", stream_->preliminary_headers().find(":status")->second);
  EXPECT_EQ(0u, stream_->response_headers().size());
  EXPECT_EQ(100, stream_->response_code());
  EXPECT_EQ("", stream_->data());
}

TEST_P(QuicSpdyClientStreamTest, TestFramingOnePacket) {
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(body_.length(), &buffer);
  QuicString header = QuicString(buffer.get(), header_length);
  QuicString data = VersionHasDataFrameHeader(connection_->transport_version())
                        ? header + body_
                        : body_;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  EXPECT_EQ("200", stream_->response_headers().find(":status")->second);
  EXPECT_EQ(200, stream_->response_code());
  EXPECT_EQ(body_, stream_->data());
}

TEST_P(QuicSpdyClientStreamTest,
       QUIC_TEST_DISABLED_IN_CHROME(TestFramingExtraData)) {
  QuicString large_body = "hello world!!!!!!";

  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);
  // The headers should parse successfully.
  EXPECT_EQ(QUIC_STREAM_NO_ERROR, stream_->stream_error());
  EXPECT_EQ("200", stream_->response_headers().find(":status")->second);
  EXPECT_EQ(200, stream_->response_code());
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(large_body.length(), &buffer);
  QuicString header = QuicString(buffer.get(), header_length);
  QuicString data = VersionHasDataFrameHeader(connection_->transport_version())
                        ? header + large_body
                        : large_body;
  EXPECT_CALL(*connection_, SendControlFrame(_));
  EXPECT_CALL(*connection_,
              OnStreamReset(stream_->id(), QUIC_BAD_APPLICATION_PAYLOAD));

  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));

  EXPECT_NE(QUIC_STREAM_NO_ERROR, stream_->stream_error());
}

TEST_P(QuicSpdyClientStreamTest, ReceivingTrailers) {
  // Test that receiving trailing headers, containing a final offset, results in
  // the stream being closed at that byte offset.

  // Send headers as usual.
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);

  // Send trailers before sending the body. Even though a FIN has been received
  // the stream should not be closed, as it does not yet have all the data bytes
  // promised by the final offset field.
  SpdyHeaderBlock trailer_block;
  trailer_block["trailer key"] = "trailer value";
  trailer_block[kFinalOffsetHeaderKey] =
      QuicTextUtils::Uint64ToString(body_.size());
  auto trailers = AsHeaderList(trailer_block);
  stream_->OnStreamHeaderList(true, trailers.uncompressed_header_bytes(),
                              trailers);

  // Now send the body, which should close the stream as the FIN has been
  // received, as well as all data.
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(body_.length(), &buffer);
  QuicString header = QuicString(buffer.get(), header_length);
  QuicString data = VersionHasDataFrameHeader(connection_->transport_version())
                        ? header + body_
                        : body_;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  EXPECT_TRUE(stream_->reading_stopped());
}

}  // namespace
}  // namespace test
}  // namespace quic
