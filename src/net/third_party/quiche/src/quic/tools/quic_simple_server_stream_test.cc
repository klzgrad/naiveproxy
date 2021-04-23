// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/tools/quic_simple_server_stream.h"

#include <list>
#include <memory>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quic/core/crypto/null_encrypter.h"
#include "quic/core/http/http_encoder.h"
#include "quic/core/http/spdy_utils.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/platform/api/quic_expect_bug.h"
#include "quic/platform/api/quic_ptr_util.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/crypto_test_utils.h"
#include "quic/test_tools/quic_config_peer.h"
#include "quic/test_tools/quic_connection_peer.h"
#include "quic/test_tools/quic_session_peer.h"
#include "quic/test_tools/quic_spdy_session_peer.h"
#include "quic/test_tools/quic_stream_peer.h"
#include "quic/test_tools/quic_test_utils.h"
#include "quic/tools/quic_backend_response.h"
#include "quic/tools/quic_memory_cache_backend.h"
#include "quic/tools/quic_simple_server_session.h"

using testing::_;
using testing::AnyNumber;
using testing::InSequence;
using testing::Invoke;
using testing::StrictMock;

namespace quic {
namespace test {

const size_t kFakeFrameLen = 60;
const size_t kErrorLength = strlen(QuicSimpleServerStream::kErrorResponseBody);
const size_t kDataFrameHeaderLength = 2;

class TestStream : public QuicSimpleServerStream {
 public:
  TestStream(QuicStreamId stream_id,
             QuicSpdySession* session,
             StreamType type,
             QuicSimpleServerBackend* quic_simple_server_backend)
      : QuicSimpleServerStream(stream_id,
                               session,
                               type,
                               quic_simple_server_backend) {}

  ~TestStream() override = default;

  MOCK_METHOD(void, WriteHeadersMock, (bool fin), ());

  size_t WriteHeaders(spdy::Http2HeaderBlock /*header_block*/,
                      bool fin,
                      QuicReferenceCountedPointer<QuicAckListenerInterface>
                      /*ack_listener*/) override {
    WriteHeadersMock(fin);
    return 0;
  }

  // Expose protected QuicSimpleServerStream methods.
  void DoSendResponse() { SendResponse(); }
  void DoSendErrorResponse() { SendErrorResponse(); }

  spdy::Http2HeaderBlock* mutable_headers() { return &request_headers_; }
  void set_body(std::string body) { body_ = std::move(body); }
  const std::string& body() const { return body_; }
  int content_length() const { return content_length_; }
  bool send_response_was_called() const { return send_response_was_called_; }

  absl::string_view GetHeader(absl::string_view key) const {
    auto it = request_headers_.find(key);
    QUICHE_DCHECK(it != request_headers_.end());
    return it->second;
  }

 protected:
  void SendResponse() override {
    send_response_was_called_ = true;
    QuicSimpleServerStream::SendResponse();
  }

 private:
  bool send_response_was_called_ = false;
};

namespace {

class MockQuicSimpleServerSession : public QuicSimpleServerSession {
 public:
  const size_t kMaxStreamsForTest = 100;

  MockQuicSimpleServerSession(
      QuicConnection* connection,
      MockQuicSessionVisitor* owner,
      MockQuicCryptoServerStreamHelper* helper,
      QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      QuicSimpleServerBackend* quic_simple_server_backend)
      : QuicSimpleServerSession(DefaultQuicConfig(),
                                CurrentSupportedVersions(),
                                connection,
                                owner,
                                helper,
                                crypto_config,
                                compressed_certs_cache,
                                quic_simple_server_backend) {
    if (VersionHasIetfQuicFrames(connection->transport_version())) {
      QuicSessionPeer::SetMaxOpenIncomingUnidirectionalStreams(
          this, kMaxStreamsForTest);
      QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(
          this, kMaxStreamsForTest);
    } else {
      QuicSessionPeer::SetMaxOpenIncomingStreams(this, kMaxStreamsForTest);
      QuicSessionPeer::SetMaxOpenOutgoingStreams(this, kMaxStreamsForTest);
    }
    ON_CALL(*this, WritevData(_, _, _, _, _, _))
        .WillByDefault(Invoke(this, &MockQuicSimpleServerSession::ConsumeData));
  }

  MockQuicSimpleServerSession(const MockQuicSimpleServerSession&) = delete;
  MockQuicSimpleServerSession& operator=(const MockQuicSimpleServerSession&) =
      delete;
  ~MockQuicSimpleServerSession() override = default;

  MOCK_METHOD(void,
              OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicConsumedData,
              WritevData,
              (QuicStreamId id,
               size_t write_length,
               QuicStreamOffset offset,
               StreamSendingState state,
               TransmissionType type,
               absl::optional<EncryptionLevel> level),
              (override));
  MOCK_METHOD(void,
              OnStreamHeaderList,
              (QuicStreamId stream_id,
               bool fin,
               size_t frame_len,
               const QuicHeaderList& header_list),
              (override));
  MOCK_METHOD(void,
              OnStreamHeadersPriority,
              (QuicStreamId stream_id,
               const spdy::SpdyStreamPrecedence& precedence),
              (override));
  MOCK_METHOD(void,
              MaybeSendRstStreamFrame,
              (QuicStreamId stream_id,
               QuicRstStreamErrorCode error,
               QuicStreamOffset bytes_written),
              (override));
  MOCK_METHOD(void,
              MaybeSendStopSendingFrame,
              (QuicStreamId stream_id, QuicRstStreamErrorCode error),
              (override));
  // Matchers cannot be used on non-copyable types like Http2HeaderBlock.
  void PromisePushResources(
      const std::string& request_url,
      const std::list<QuicBackendResponse::ServerPushInfo>& resources,
      QuicStreamId original_stream_id,
      const spdy::SpdyStreamPrecedence& original_precedence,
      const spdy::Http2HeaderBlock& original_request_headers) override {
    original_request_headers_ = original_request_headers.Clone();
    PromisePushResourcesMock(request_url, resources, original_stream_id,
                             original_precedence, original_request_headers);
  }
  MOCK_METHOD(void,
              PromisePushResourcesMock,
              (const std::string&,
               const std::list<QuicBackendResponse::ServerPushInfo>&,
               QuicStreamId,
               const spdy::SpdyStreamPrecedence&,
               const spdy::Http2HeaderBlock&),
              ());

  using QuicSession::ActivateStream;

  QuicConsumedData ConsumeData(QuicStreamId id,
                               size_t write_length,
                               QuicStreamOffset offset,
                               StreamSendingState state,
                               TransmissionType /*type*/,
                               absl::optional<EncryptionLevel> /*level*/) {
    if (write_length > 0) {
      auto buf = std::make_unique<char[]>(write_length);
      QuicStream* stream = GetOrCreateStream(id);
      QUICHE_DCHECK(stream);
      QuicDataWriter writer(write_length, buf.get(), quiche::HOST_BYTE_ORDER);
      stream->WriteStreamData(offset, write_length, &writer);
    } else {
      QUICHE_DCHECK(state != NO_FIN);
    }
    return QuicConsumedData(write_length, state != NO_FIN);
  }

  spdy::Http2HeaderBlock original_request_headers_;
};

class QuicSimpleServerStreamTest : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  QuicSimpleServerStreamTest()
      : connection_(
            new StrictMock<MockQuicConnection>(&helper_,
                                               &alarm_factory_,
                                               Perspective::IS_SERVER,
                                               SupportedVersions(GetParam()))),
        crypto_config_(new QuicCryptoServerConfig(
            QuicCryptoServerConfig::TESTING,
            QuicRandom::GetInstance(),
            crypto_test_utils::ProofSourceForTesting(),
            KeyExchangeSource::Default())),
        compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize),
        session_(connection_,
                 &session_owner_,
                 &session_helper_,
                 crypto_config_.get(),
                 &compressed_certs_cache_,
                 &memory_cache_backend_),
        quic_response_(new QuicBackendResponse),
        body_("hello world") {
    connection_->set_visitor(&session_);
    header_list_.OnHeaderBlockStart();
    header_list_.OnHeader(":authority", "www.google.com");
    header_list_.OnHeader(":path", "/");
    header_list_.OnHeader(":method", "POST");
    header_list_.OnHeader("content-length", "11");

    header_list_.OnHeaderBlockEnd(128, 128);

    // New streams rely on having the peer's flow control receive window
    // negotiated in the config.
    session_.config()->SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindowForTest);
    session_.config()->SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindowForTest);
    session_.Initialize();
    connection_->SetEncrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::NullEncrypter>(connection_->perspective()));
    if (connection_->version().SupportsAntiAmplificationLimit()) {
      QuicConnectionPeer::SetAddressValidated(connection_);
    }
    stream_ = new StrictMock<TestStream>(
        GetNthClientInitiatedBidirectionalStreamId(
            connection_->transport_version(), 0),
        &session_, BIDIRECTIONAL, &memory_cache_backend_);
    // Register stream_ in dynamic_stream_map_ and pass ownership to session_.
    session_.ActivateStream(QuicWrapUnique(stream_));
    QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
        session_.config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(session_.config(), 10);
    session_.OnConfigNegotiated();
    connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }

  const std::string& StreamBody() { return stream_->body(); }

  std::string StreamHeadersValue(const std::string& key) {
    return (*stream_->mutable_headers())[key].as_string();
  }

  bool UsesHttp3() const {
    return VersionUsesHttp3(connection_->transport_version());
  }

  spdy::Http2HeaderBlock response_headers_;
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  StrictMock<MockQuicConnection>* connection_;
  StrictMock<MockQuicSessionVisitor> session_owner_;
  StrictMock<MockQuicCryptoServerStreamHelper> session_helper_;
  std::unique_ptr<QuicCryptoServerConfig> crypto_config_;
  QuicCompressedCertsCache compressed_certs_cache_;
  QuicMemoryCacheBackend memory_cache_backend_;
  StrictMock<MockQuicSimpleServerSession> session_;
  StrictMock<TestStream>* stream_;  // Owned by session_.
  std::unique_ptr<QuicBackendResponse> quic_response_;
  std::string body_;
  QuicHeaderList header_list_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSimpleServerStreamTest,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSimpleServerStreamTest, TestFraming) {
  EXPECT_CALL(session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke(&session_, &MockQuicSimpleServerSession::ConsumeData));
  stream_->OnStreamHeaderList(false, kFakeFrameLen, header_list_);
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body_.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);
  std::string data = UsesHttp3() ? header + body_ : body_;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  EXPECT_EQ("11", StreamHeadersValue("content-length"));
  EXPECT_EQ("/", StreamHeadersValue(":path"));
  EXPECT_EQ("POST", StreamHeadersValue(":method"));
  EXPECT_EQ(body_, StreamBody());
}

TEST_P(QuicSimpleServerStreamTest, TestFramingOnePacket) {
  EXPECT_CALL(session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke(&session_, &MockQuicSimpleServerSession::ConsumeData));

  stream_->OnStreamHeaderList(false, kFakeFrameLen, header_list_);
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body_.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);
  std::string data = UsesHttp3() ? header + body_ : body_;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  EXPECT_EQ("11", StreamHeadersValue("content-length"));
  EXPECT_EQ("/", StreamHeadersValue(":path"));
  EXPECT_EQ("POST", StreamHeadersValue(":method"));
  EXPECT_EQ(body_, StreamBody());
}

TEST_P(QuicSimpleServerStreamTest, SendQuicRstStreamNoErrorInStopReading) {
  EXPECT_CALL(session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke(&session_, &MockQuicSimpleServerSession::ConsumeData));

  EXPECT_FALSE(stream_->fin_received());
  EXPECT_FALSE(stream_->rst_received());

  QuicStreamPeer::SetFinSent(stream_);
  stream_->CloseWriteSide();

  if (session_.version().UsesHttp3()) {
    EXPECT_CALL(session_, MaybeSendStopSendingFrame(_, QUIC_STREAM_NO_ERROR))
        .Times(1);
  } else {
    EXPECT_CALL(session_, MaybeSendRstStreamFrame(_, QUIC_STREAM_NO_ERROR, _))
        .Times(1);
  }
  stream_->StopReading();
}

TEST_P(QuicSimpleServerStreamTest, TestFramingExtraData) {
  InSequence seq;
  std::string large_body = "hello world!!!!!!";

  // We'll automatically write out an error (headers + body)
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  if (UsesHttp3()) {
    EXPECT_CALL(session_,
                WritevData(_, kDataFrameHeaderLength, _, NO_FIN, _, _));
  }
  EXPECT_CALL(session_, WritevData(_, kErrorLength, _, FIN, _, _));

  stream_->OnStreamHeaderList(false, kFakeFrameLen, header_list_);
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body_.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);
  std::string data = UsesHttp3() ? header + body_ : body_;

  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  // Content length is still 11.  This will register as an error and we won't
  // accept the bytes.
  header_length =
      HttpEncoder::SerializeDataFrameHeader(large_body.length(), &buffer);
  header = std::string(buffer.get(), header_length);
  std::string data2 = UsesHttp3() ? header + large_body : large_body;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/true, data.size(), data2));
  EXPECT_EQ("11", StreamHeadersValue("content-length"));
  EXPECT_EQ("/", StreamHeadersValue(":path"));
  EXPECT_EQ("POST", StreamHeadersValue(":method"));
}

TEST_P(QuicSimpleServerStreamTest, SendResponseWithIllegalResponseStatus) {
  // Send an illegal response with response status not supported by HTTP/2.
  spdy::Http2HeaderBlock* request_headers = stream_->mutable_headers();
  (*request_headers)[":path"] = "/bar";
  (*request_headers)[":authority"] = "www.google.com";
  (*request_headers)[":method"] = "GET";

  // HTTP/2 only supports integer responsecode, so "200 OK" is illegal.
  response_headers_[":status"] = "200 OK";
  response_headers_["content-length"] = "5";
  std::string body = "Yummm";
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);

  memory_cache_backend_.AddResponse("www.google.com", "/bar",
                                    std::move(response_headers_), body);

  QuicStreamPeer::SetFinReceived(stream_);

  InSequence s;
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  if (UsesHttp3()) {
    EXPECT_CALL(session_, WritevData(_, header_length, _, NO_FIN, _, _));
  }
  EXPECT_CALL(session_, WritevData(_, kErrorLength, _, FIN, _, _));

  stream_->DoSendResponse();
  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, SendResponseWithIllegalResponseStatus2) {
  // Send an illegal response with response status not supported by HTTP/2.
  spdy::Http2HeaderBlock* request_headers = stream_->mutable_headers();
  (*request_headers)[":path"] = "/bar";
  (*request_headers)[":authority"] = "www.google.com";
  (*request_headers)[":method"] = "GET";

  // HTTP/2 only supports 3-digit-integer, so "+200" is illegal.
  response_headers_[":status"] = "+200";
  response_headers_["content-length"] = "5";
  std::string body = "Yummm";

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);

  memory_cache_backend_.AddResponse("www.google.com", "/bar",
                                    std::move(response_headers_), body);

  QuicStreamPeer::SetFinReceived(stream_);

  InSequence s;
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  if (UsesHttp3()) {
    EXPECT_CALL(session_, WritevData(_, header_length, _, NO_FIN, _, _));
  }
  EXPECT_CALL(session_, WritevData(_, kErrorLength, _, FIN, _, _));

  stream_->DoSendResponse();
  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, SendPushResponseWith404Response) {
  // Create a new promised stream with even id().
  auto promised_stream = new StrictMock<TestStream>(
      GetNthServerInitiatedUnidirectionalStreamId(
          connection_->transport_version(), 3),
      &session_, WRITE_UNIDIRECTIONAL, &memory_cache_backend_);
  session_.ActivateStream(QuicWrapUnique(promised_stream));

  // Send a push response with response status 404, which will be regarded as
  // invalid server push response.
  spdy::Http2HeaderBlock* request_headers = promised_stream->mutable_headers();
  (*request_headers)[":path"] = "/bar";
  (*request_headers)[":authority"] = "www.google.com";
  (*request_headers)[":method"] = "GET";

  response_headers_[":status"] = "404";
  response_headers_["content-length"] = "8";
  std::string body = "NotFound";

  memory_cache_backend_.AddResponse("www.google.com", "/bar",
                                    std::move(response_headers_), body);

  InSequence s;
  if (session_.version().UsesHttp3()) {
    EXPECT_CALL(session_, MaybeSendStopSendingFrame(promised_stream->id(),
                                                    QUIC_STREAM_CANCELLED));
  }
  EXPECT_CALL(session_, MaybeSendRstStreamFrame(promised_stream->id(),
                                                QUIC_STREAM_CANCELLED, 0));

  promised_stream->DoSendResponse();
}

TEST_P(QuicSimpleServerStreamTest, SendResponseWithValidHeaders) {
  // Add a request and response with valid headers.
  spdy::Http2HeaderBlock* request_headers = stream_->mutable_headers();
  (*request_headers)[":path"] = "/bar";
  (*request_headers)[":authority"] = "www.google.com";
  (*request_headers)[":method"] = "GET";

  response_headers_[":status"] = "200";
  response_headers_["content-length"] = "5";
  std::string body = "Yummm";

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);

  memory_cache_backend_.AddResponse("www.google.com", "/bar",
                                    std::move(response_headers_), body);
  QuicStreamPeer::SetFinReceived(stream_);

  InSequence s;
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  if (UsesHttp3()) {
    EXPECT_CALL(session_, WritevData(_, header_length, _, NO_FIN, _, _));
  }
  EXPECT_CALL(session_, WritevData(_, body.length(), _, FIN, _, _));

  stream_->DoSendResponse();
  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, SendResponseWithPushResources) {
  // Tests that if a response has push resources to be send, SendResponse() will
  // call PromisePushResources() to handle these resources.

  // Add a request and response with valid headers into cache.
  std::string host = "www.google.com";
  std::string request_path = "/foo";
  std::string body = "Yummm";
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);
  QuicBackendResponse::ServerPushInfo push_info(
      QuicUrl(host, "/bar"), spdy::Http2HeaderBlock(),
      QuicStream::kDefaultPriority, "Push body");
  std::list<QuicBackendResponse::ServerPushInfo> push_resources;
  push_resources.push_back(push_info);
  memory_cache_backend_.AddSimpleResponseWithServerPushResources(
      host, request_path, 200, body, push_resources);

  spdy::Http2HeaderBlock* request_headers = stream_->mutable_headers();
  (*request_headers)[":path"] = request_path;
  (*request_headers)[":authority"] = host;
  (*request_headers)[":method"] = "GET";

  QuicStreamPeer::SetFinReceived(stream_);
  InSequence s;
  EXPECT_CALL(session_, PromisePushResourcesMock(
                            host + request_path, _,
                            GetNthClientInitiatedBidirectionalStreamId(
                                connection_->transport_version(), 0),
                            _, _));
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  if (UsesHttp3()) {
    EXPECT_CALL(session_, WritevData(_, header_length, _, NO_FIN, _, _));
  }
  EXPECT_CALL(session_, WritevData(_, body.length(), _, FIN, _, _));
  stream_->DoSendResponse();
  EXPECT_EQ(*request_headers, session_.original_request_headers_);
}

TEST_P(QuicSimpleServerStreamTest, PushResponseOnClientInitiatedStream) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (GetParam() != AllSupportedVersions()[0]) {
    return;
  }

  // Calling PushResponse() on a client initialted stream is never supposed to
  // happen.
  EXPECT_QUIC_BUG(stream_->PushResponse(spdy::Http2HeaderBlock()),
                  "Client initiated stream"
                  " shouldn't be used as promised stream.");
}

TEST_P(QuicSimpleServerStreamTest, PushResponseOnServerInitiatedStream) {
  // Tests that PushResponse() should take the given headers as request headers
  // and fetch response from cache, and send it out.

  // Create a stream with even stream id and test against this stream.
  const QuicStreamId kServerInitiatedStreamId =
      GetNthServerInitiatedUnidirectionalStreamId(
          connection_->transport_version(), 3);
  // Create a server initiated stream and pass it to session_.
  auto server_initiated_stream =
      new StrictMock<TestStream>(kServerInitiatedStreamId, &session_,
                                 WRITE_UNIDIRECTIONAL, &memory_cache_backend_);
  session_.ActivateStream(QuicWrapUnique(server_initiated_stream));

  const std::string kHost = "www.foo.com";
  const std::string kPath = "/bar";
  spdy::Http2HeaderBlock headers;
  headers[":path"] = kPath;
  headers[":authority"] = kHost;
  headers[":method"] = "GET";

  response_headers_[":status"] = "200";
  response_headers_["content-length"] = "5";
  const std::string kBody = "Hello";
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(kBody.length(), &buffer);
  memory_cache_backend_.AddResponse(kHost, kPath, std::move(response_headers_),
                                    kBody);

  // Call PushResponse() should trigger stream to fetch response from cache
  // and send it back.
  InSequence s;
  EXPECT_CALL(*server_initiated_stream, WriteHeadersMock(false));

  if (UsesHttp3()) {
    EXPECT_CALL(session_, WritevData(kServerInitiatedStreamId, header_length, _,
                                     NO_FIN, _, _));
  }
  EXPECT_CALL(session_,
              WritevData(kServerInitiatedStreamId, kBody.size(), _, FIN, _, _));
  server_initiated_stream->PushResponse(std::move(headers));
  EXPECT_EQ(kPath, server_initiated_stream->GetHeader(":path"));
  EXPECT_EQ("GET", server_initiated_stream->GetHeader(":method"));
}

TEST_P(QuicSimpleServerStreamTest, TestSendErrorResponse) {
  QuicStreamPeer::SetFinReceived(stream_);

  InSequence s;
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  if (UsesHttp3()) {
    EXPECT_CALL(session_,
                WritevData(_, kDataFrameHeaderLength, _, NO_FIN, _, _));
  }
  EXPECT_CALL(session_, WritevData(_, kErrorLength, _, FIN, _, _));

  stream_->DoSendErrorResponse();
  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, InvalidMultipleContentLength) {
  spdy::Http2HeaderBlock request_headers;
  // \000 is a way to write the null byte when followed by a literal digit.
  header_list_.OnHeader("content-length", absl::string_view("11\00012", 5));

  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  EXPECT_CALL(session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke(&session_, &MockQuicSimpleServerSession::ConsumeData));
  stream_->OnStreamHeaderList(true, kFakeFrameLen, header_list_);

  EXPECT_TRUE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_TRUE(stream_->reading_stopped());
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, InvalidLeadingNullContentLength) {
  spdy::Http2HeaderBlock request_headers;
  // \000 is a way to write the null byte when followed by a literal digit.
  header_list_.OnHeader("content-length", absl::string_view("\00012", 3));

  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  EXPECT_CALL(session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke(&session_, &MockQuicSimpleServerSession::ConsumeData));
  stream_->OnStreamHeaderList(true, kFakeFrameLen, header_list_);

  EXPECT_TRUE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_TRUE(stream_->reading_stopped());
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, ValidMultipleContentLength) {
  spdy::Http2HeaderBlock request_headers;
  // \000 is a way to write the null byte when followed by a literal digit.
  header_list_.OnHeader("content-length", absl::string_view("11\00011", 5));

  stream_->OnStreamHeaderList(false, kFakeFrameLen, header_list_);

  EXPECT_EQ(11, stream_->content_length());
  EXPECT_FALSE(QuicStreamPeer::read_side_closed(stream_));
  EXPECT_FALSE(stream_->reading_stopped());
  EXPECT_FALSE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest,
       DoNotSendQuicRstStreamNoErrorWithRstReceived) {
  EXPECT_FALSE(stream_->reading_stopped());

  if (VersionUsesHttp3(connection_->transport_version())) {
    // Unidirectional stream type and then a Stream Cancellation instruction is
    // sent on the QPACK decoder stream.  Ignore these writes without any
    // assumption on their number or size.
    auto* qpack_decoder_stream =
        QuicSpdySessionPeer::GetQpackDecoderSendStream(&session_);
    EXPECT_CALL(session_, WritevData(qpack_decoder_stream->id(), _, _, _, _, _))
        .Times(AnyNumber());
  }

  EXPECT_CALL(session_, MaybeSendRstStreamFrame(_,
                                                session_.version().UsesHttp3()
                                                    ? QUIC_STREAM_CANCELLED
                                                    : QUIC_RST_ACKNOWLEDGEMENT,
                                                _))
      .Times(1);
  QuicRstStreamFrame rst_frame(kInvalidControlFrameId, stream_->id(),
                               QUIC_STREAM_CANCELLED, 1234);
  stream_->OnStreamReset(rst_frame);
  if (VersionHasIetfQuicFrames(connection_->transport_version())) {
    EXPECT_CALL(session_owner_, OnStopSendingReceived(_));
    // Create and inject a STOP SENDING frame to complete the close
    // of the stream. This is only needed for version 99/IETF QUIC.
    QuicStopSendingFrame stop_sending(kInvalidControlFrameId, stream_->id(),
                                      QUIC_STREAM_CANCELLED);
    session_.OnStopSendingFrame(stop_sending);
  }
  EXPECT_TRUE(stream_->reading_stopped());
  EXPECT_TRUE(stream_->write_side_closed());
}

TEST_P(QuicSimpleServerStreamTest, InvalidHeadersWithFin) {
  char arr[] = {
      0x3a, 0x68, 0x6f, 0x73,  // :hos
      0x74, 0x00, 0x00, 0x00,  // t...
      0x00, 0x00, 0x00, 0x00,  // ....
      0x07, 0x3a, 0x6d, 0x65,  // .:me
      0x74, 0x68, 0x6f, 0x64,  // thod
      0x00, 0x00, 0x00, 0x03,  // ....
      0x47, 0x45, 0x54, 0x00,  // GET.
      0x00, 0x00, 0x05, 0x3a,  // ...:
      0x70, 0x61, 0x74, 0x68,  // path
      0x00, 0x00, 0x00, 0x04,  // ....
      0x2f, 0x66, 0x6f, 0x6f,  // /foo
      0x00, 0x00, 0x00, 0x07,  // ....
      0x3a, 0x73, 0x63, 0x68,  // :sch
      0x65, 0x6d, 0x65, 0x00,  // eme.
      0x00, 0x00, 0x00, 0x00,  // ....
      0x00, 0x00, 0x08, 0x3a,  // ...:
      0x76, 0x65, 0x72, 0x73,  // vers
      0x96, 0x6f, 0x6e, 0x00,  // <i(69)>on.
      0x00, 0x00, 0x08, 0x48,  // ...H
      0x54, 0x54, 0x50, 0x2f,  // TTP/
      0x31, 0x2e, 0x31,        // 1.1
  };
  absl::string_view data(arr, ABSL_ARRAYSIZE(arr));
  QuicStreamFrame frame(stream_->id(), true, 0, data);
  // Verify that we don't crash when we get a invalid headers in stream frame.
  stream_->OnStreamFrame(frame);
}

TEST_P(QuicSimpleServerStreamTest, ConnectSendsResponseBeforeFinReceived) {
  EXPECT_CALL(session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(
          Invoke(&session_, &MockQuicSimpleServerSession::ConsumeData));
  QuicHeaderList header_list;
  header_list.OnHeaderBlockStart();
  header_list.OnHeader(":authority", "www.google.com:4433");
  header_list.OnHeader(":method", "CONNECT-SILLY");
  header_list.OnHeaderBlockEnd(128, 128);
  EXPECT_CALL(*stream_, WriteHeadersMock(/*fin=*/false));
  stream_->OnStreamHeaderList(/*fin=*/false, kFakeFrameLen, header_list);
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body_.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);
  std::string data = UsesHttp3() ? header + body_ : body_;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), /*fin=*/false, /*offset=*/0, data));
  EXPECT_EQ("CONNECT-SILLY", StreamHeadersValue(":method"));
  EXPECT_EQ(body_, StreamBody());
  EXPECT_TRUE(stream_->send_response_was_called());
}

}  // namespace
}  // namespace test
}  // namespace quic
