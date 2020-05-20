// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_sequencer_buffer.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_flow_controller_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

using spdy::kV3HighestPriority;
using spdy::kV3LowestPriority;
using spdy::SpdyHeaderBlock;
using spdy::SpdyPriority;
using testing::_;
using testing::AtLeast;
using testing::ElementsAre;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::MatchesRegex;
using testing::Pair;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

const bool kShouldProcessData = true;
const char kDataFramePayload[] = "some data";

class TestCryptoStream : public QuicCryptoStream, public QuicCryptoHandshaker {
 public:
  explicit TestCryptoStream(QuicSession* session)
      : QuicCryptoStream(session),
        QuicCryptoHandshaker(this, session),
        encryption_established_(false),
        one_rtt_keys_available_(false),
        params_(new QuicCryptoNegotiatedParameters) {
    // Simulate a negotiated cipher_suite with a fake value.
    params_->cipher_suite = 1;
  }

  void OnHandshakeMessage(const CryptoHandshakeMessage& /*message*/) override {
    encryption_established_ = true;
    one_rtt_keys_available_ = true;
    QuicErrorCode error;
    std::string error_details;
    session()->config()->SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindowForTest);
    session()->config()->SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindowForTest);
    if (session()->connection()->version().handshake_protocol ==
        PROTOCOL_TLS1_3) {
      TransportParameters transport_parameters;
      EXPECT_TRUE(
          session()->config()->FillTransportParameters(&transport_parameters));
      error = session()->config()->ProcessTransportParameters(
          transport_parameters, CLIENT, &error_details);
    } else {
      CryptoHandshakeMessage msg;
      session()->config()->ToHandshakeMessage(&msg, transport_version());
      error =
          session()->config()->ProcessPeerHello(msg, CLIENT, &error_details);
    }
    EXPECT_THAT(error, IsQuicNoError());
    session()->OnNewEncryptionKeyAvailable(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullEncrypter>(session()->perspective()));
    session()->OnConfigNegotiated();
    if (session()->connection()->version().handshake_protocol ==
        PROTOCOL_TLS1_3) {
      session()->OnOneRttKeysAvailable();
    } else {
      session()->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    }
    session()->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
  }

  // QuicCryptoStream implementation
  bool encryption_established() const override {
    return encryption_established_;
  }
  bool one_rtt_keys_available() const override {
    return one_rtt_keys_available_;
  }
  HandshakeState GetHandshakeState() const override {
    return one_rtt_keys_available() ? HANDSHAKE_COMPLETE : HANDSHAKE_START;
  }
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override {
    return *params_;
  }
  CryptoMessageParser* crypto_message_parser() override {
    return QuicCryptoHandshaker::crypto_message_parser();
  }
  void OnPacketDecrypted(EncryptionLevel /*level*/) override {}
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakeDoneReceived() override {}

  MOCK_METHOD0(OnCanWrite, void());

  bool HasPendingCryptoRetransmission() const override { return false; }

  MOCK_CONST_METHOD0(HasPendingRetransmission, bool());

 private:
  using QuicCryptoStream::session;

  bool encryption_established_;
  bool one_rtt_keys_available_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
};

class TestStream : public QuicSpdyStream {
 public:
  TestStream(QuicStreamId id,
             QuicSpdySession* session,
             bool should_process_data)
      : QuicSpdyStream(id, session, BIDIRECTIONAL),
        should_process_data_(should_process_data) {}
  ~TestStream() override = default;

  using QuicSpdyStream::set_ack_listener;
  using QuicStream::CloseWriteSide;
  using QuicStream::WriteOrBufferData;

  void OnBodyAvailable() override {
    if (!should_process_data_) {
      return;
    }
    char buffer[2048];
    struct iovec vec;
    vec.iov_base = buffer;
    vec.iov_len = QUICHE_ARRAYSIZE(buffer);
    size_t bytes_read = Readv(&vec, 1);
    data_ += std::string(buffer, bytes_read);
  }

  MOCK_METHOD1(WriteHeadersMock, void(bool fin));

  size_t WriteHeadersImpl(spdy::SpdyHeaderBlock header_block,
                          bool fin,
                          QuicReferenceCountedPointer<QuicAckListenerInterface>
                          /*ack_listener*/) override {
    saved_headers_ = std::move(header_block);
    WriteHeadersMock(fin);
    if (VersionUsesHttp3(transport_version())) {
      // In this case, call QuicSpdyStream::WriteHeadersImpl() that does the
      // actual work of closing the stream.
      QuicSpdyStream::WriteHeadersImpl(saved_headers_.Clone(), fin, nullptr);
    }
    return 0;
  }

  const std::string& data() const { return data_; }
  const spdy::SpdyHeaderBlock& saved_headers() const { return saved_headers_; }

  // Expose protected accessor.
  const QuicStreamSequencer* sequencer() const {
    return QuicStream::sequencer();
  }

 private:
  bool should_process_data_;
  spdy::SpdyHeaderBlock saved_headers_;
  std::string data_;
};

class TestSession : public MockQuicSpdySession {
 public:
  explicit TestSession(QuicConnection* connection)
      : MockQuicSpdySession(connection, /*create_mock_crypto_stream=*/false),
        crypto_stream_(this) {}

  TestCryptoStream* GetMutableCryptoStream() override {
    return &crypto_stream_;
  }

  const TestCryptoStream* GetCryptoStream() const override {
    return &crypto_stream_;
  }

 private:
  StrictMock<TestCryptoStream> crypto_stream_;
};

class TestMockUpdateStreamSession : public MockQuicSpdySession {
 public:
  explicit TestMockUpdateStreamSession(QuicConnection* connection)
      : MockQuicSpdySession(connection),
        expected_precedence_(
            spdy::SpdyStreamPrecedence(QuicStream::kDefaultPriority)) {}

  void UpdateStreamPriority(
      QuicStreamId id,
      const spdy::SpdyStreamPrecedence& precedence) override {
    EXPECT_EQ(id, expected_stream_->id());
    EXPECT_EQ(expected_precedence_, precedence);
    EXPECT_EQ(expected_precedence_, expected_stream_->precedence());
  }

  void SetExpectedStream(QuicSpdyStream* stream) { expected_stream_ = stream; }
  void SetExpectedPriority(const spdy::SpdyStreamPrecedence& precedence) {
    expected_precedence_ = precedence;
  }

 private:
  QuicSpdyStream* expected_stream_;
  spdy::SpdyStreamPrecedence expected_precedence_;
};

class QuicSpdyStreamTest : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  QuicSpdyStreamTest() {
    headers_[":host"] = "www.google.com";
    headers_[":path"] = "/index.hml";
    headers_[":scheme"] = "https";
    headers_["cookie"] =
        "__utma=208381060.1228362404.1372200928.1372200928.1372200928.1; "
        "__utmc=160408618; "
        "GX=DQAAAOEAAACWJYdewdE9rIrW6qw3PtVi2-d729qaa-74KqOsM1NVQblK4VhX"
        "hoALMsy6HOdDad2Sz0flUByv7etmo3mLMidGrBoljqO9hSVA40SLqpG_iuKKSHX"
        "RW3Np4bq0F0SDGDNsW0DSmTS9ufMRrlpARJDS7qAI6M3bghqJp4eABKZiRqebHT"
        "pMU-RXvTI5D5oCF1vYxYofH_l1Kviuiy3oQ1kS1enqWgbhJ2t61_SNdv-1XJIS0"
        "O3YeHLmVCs62O6zp89QwakfAWK9d3IDQvVSJzCQsvxvNIvaZFa567MawWlXg0Rh"
        "1zFMi5vzcns38-8_Sns; "
        "GA=v*2%2Fmem*57968640*47239936%2Fmem*57968640*47114716%2Fno-nm-"
        "yj*15%2Fno-cc-yj*5%2Fpc-ch*133685%2Fpc-s-cr*133947%2Fpc-s-t*1339"
        "47%2Fno-nm-yj*4%2Fno-cc-yj*1%2Fceft-as*1%2Fceft-nqas*0%2Fad-ra-c"
        "v_p%2Fad-nr-cv_p-f*1%2Fad-v-cv_p*859%2Fad-ns-cv_p-f*1%2Ffn-v-ad%"
        "2Fpc-t*250%2Fpc-cm*461%2Fpc-s-cr*722%2Fpc-s-t*722%2Fau_p*4"
        "SICAID=AJKiYcHdKgxum7KMXG0ei2t1-W4OD1uW-ecNsCqC0wDuAXiDGIcT_HA2o1"
        "3Rs1UKCuBAF9g8rWNOFbxt8PSNSHFuIhOo2t6bJAVpCsMU5Laa6lewuTMYI8MzdQP"
        "ARHKyW-koxuhMZHUnGBJAM1gJODe0cATO_KGoX4pbbFxxJ5IicRxOrWK_5rU3cdy6"
        "edlR9FsEdH6iujMcHkbE5l18ehJDwTWmBKBzVD87naobhMMrF6VvnDGxQVGp9Ir_b"
        "Rgj3RWUoPumQVCxtSOBdX0GlJOEcDTNCzQIm9BSfetog_eP_TfYubKudt5eMsXmN6"
        "QnyXHeGeK2UINUzJ-D30AFcpqYgH9_1BvYSpi7fc7_ydBU8TaD8ZRxvtnzXqj0RfG"
        "tuHghmv3aD-uzSYJ75XDdzKdizZ86IG6Fbn1XFhYZM-fbHhm3mVEXnyRW4ZuNOLFk"
        "Fas6LMcVC6Q8QLlHYbXBpdNFuGbuZGUnav5C-2I_-46lL0NGg3GewxGKGHvHEfoyn"
        "EFFlEYHsBQ98rXImL8ySDycdLEFvBPdtctPmWCfTxwmoSMLHU2SCVDhbqMWU5b0yr"
        "JBCScs_ejbKaqBDoB7ZGxTvqlrB__2ZmnHHjCr8RgMRtKNtIeuZAo ";
  }

  ~QuicSpdyStreamTest() override = default;

  // Return QPACK-encoded header block without using the dynamic table.
  std::string EncodeQpackHeaders(
      std::vector<std::pair<quiche::QuicheStringPiece,
                            quiche::QuicheStringPiece>> headers) {
    SpdyHeaderBlock header_block;
    for (const auto& header_field : headers) {
      header_block.AppendValueOrAddHeader(header_field.first,
                                          header_field.second);
    }

    return EncodeQpackHeaders(header_block);
  }

  // Return QPACK-encoded header block without using the dynamic table.
  std::string EncodeQpackHeaders(const SpdyHeaderBlock& header) {
    NoopQpackStreamSenderDelegate encoder_stream_sender_delegate;
    auto qpack_encoder = std::make_unique<QpackEncoder>(session_.get());
    qpack_encoder->set_qpack_stream_sender_delegate(
        &encoder_stream_sender_delegate);
    // QpackEncoder does not use the dynamic table by default,
    // therefore the value of |stream_id| does not matter.
    return qpack_encoder->EncodeHeaderList(/* stream_id = */ 0, header,
                                           nullptr);
  }

  void Initialize(bool stream_should_process_data) {
    InitializeWithPerspective(stream_should_process_data,
                              Perspective::IS_SERVER);
  }

  void InitializeWithPerspective(bool stream_should_process_data,
                                 Perspective perspective) {
    connection_ = new StrictMock<MockQuicConnection>(
        &helper_, &alarm_factory_, perspective, SupportedVersions(GetParam()));
    session_ = std::make_unique<StrictMock<TestSession>>(connection_);
    session_->Initialize();
    ON_CALL(*session_, WritevData(_, _, _, _, _, _))
        .WillByDefault(
            Invoke(session_.get(), &MockQuicSpdySession::ConsumeData));

    stream_ =
        new StrictMock<TestStream>(GetNthClientInitiatedBidirectionalId(0),
                                   session_.get(), stream_should_process_data);
    session_->ActivateStream(QuicWrapUnique(stream_));
    stream2_ =
        new StrictMock<TestStream>(GetNthClientInitiatedBidirectionalId(1),
                                   session_.get(), stream_should_process_data);
    session_->ActivateStream(QuicWrapUnique(stream2_));
    QuicConfigPeer::SetReceivedInitialSessionFlowControlWindow(
        session_->config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesUnidirectional(
        session_->config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesIncomingBidirectional(
        session_->config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedInitialMaxStreamDataBytesOutgoingBidirectional(
        session_->config(), kMinimumFlowControlSendWindow);
    QuicConfigPeer::SetReceivedMaxUnidirectionalStreams(session_->config(), 10);
    session_->OnConfigNegotiated();
    if (UsesHttp3()) {
      // The control stream will write the stream type, a greased frame, and
      // SETTINGS frame.
      int num_control_stream_writes = 3;
      auto send_control_stream =
          QuicSpdySessionPeer::GetSendControlStream(session_.get());
      EXPECT_CALL(*session_,
                  WritevData(send_control_stream->id(), _, _, _, _, _))
          .Times(num_control_stream_writes);
      auto qpack_decoder_stream =
          QuicSpdySessionPeer::GetQpackDecoderSendStream(session_.get());
      EXPECT_CALL(*session_,
                  WritevData(qpack_decoder_stream->id(), 1, 0, _, _, _));
      auto qpack_encoder_stream =
          QuicSpdySessionPeer::GetQpackEncoderSendStream(session_.get());
      EXPECT_CALL(*session_,
                  WritevData(qpack_encoder_stream->id(), 1, 0, _, _, _));
    }
    TestCryptoStream* crypto_stream = session_->GetMutableCryptoStream();
    EXPECT_CALL(*crypto_stream, HasPendingRetransmission())
        .Times(testing::AnyNumber());

    if (connection_->version().HasHandshakeDone() &&
        session_->perspective() == Perspective::IS_SERVER) {
      EXPECT_CALL(*connection_, SendControlFrame(_))
          .WillOnce(Invoke(&ClearControlFrame));
    }
    CryptoHandshakeMessage message;
    session_->GetMutableCryptoStream()->OnHandshakeMessage(message);
  }

  QuicHeaderList ProcessHeaders(bool fin, const SpdyHeaderBlock& headers) {
    QuicHeaderList h = AsHeaderList(headers);
    stream_->OnStreamHeaderList(fin, h.uncompressed_header_bytes(), h);
    return h;
  }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return GetNthClientInitiatedBidirectionalStreamId(
        connection_->transport_version(), n);
  }

  bool UsesHttp3() const {
    return VersionUsesHttp3(GetParam().transport_version);
  }

  // Construct HEADERS frame with QPACK-encoded |headers| without using the
  // dynamic table.
  std::string HeadersFrame(
      std::vector<std::pair<quiche::QuicheStringPiece,
                            quiche::QuicheStringPiece>> headers) {
    return HeadersFrame(EncodeQpackHeaders(headers));
  }

  // Construct HEADERS frame with QPACK-encoded |headers| without using the
  // dynamic table.
  std::string HeadersFrame(const SpdyHeaderBlock& headers) {
    return HeadersFrame(EncodeQpackHeaders(headers));
  }

  // Construct HEADERS frame with given payload.
  std::string HeadersFrame(quiche::QuicheStringPiece payload) {
    std::unique_ptr<char[]> headers_buffer;
    QuicByteCount headers_frame_header_length =
        HttpEncoder::SerializeHeadersFrameHeader(payload.length(),
                                                 &headers_buffer);
    quiche::QuicheStringPiece headers_frame_header(headers_buffer.get(),
                                                   headers_frame_header_length);
    return quiche::QuicheStrCat(headers_frame_header, payload);
  }

  std::string DataFrame(quiche::QuicheStringPiece payload) {
    std::unique_ptr<char[]> data_buffer;
    QuicByteCount data_frame_header_length =
        HttpEncoder::SerializeDataFrameHeader(payload.length(), &data_buffer);
    quiche::QuicheStringPiece data_frame_header(data_buffer.get(),
                                                data_frame_header_length);
    return quiche::QuicheStrCat(data_frame_header, payload);
  }

  std::string UnknownFrame(uint64_t frame_type,
                           quiche::QuicheStringPiece payload) {
    std::string frame;
    const size_t length = QuicDataWriter::GetVarInt62Len(frame_type) +
                          QuicDataWriter::GetVarInt62Len(payload.size()) +
                          payload.size();
    frame.resize(length);

    QuicDataWriter writer(length, const_cast<char*>(frame.data()));
    writer.WriteVarInt62(frame_type);
    writer.WriteStringPieceVarInt62(payload);
    // Even though integers can be encoded with different lengths,
    // QuicDataWriter is expected to produce an encoding in Write*() of length
    // promised in GetVarInt62Len().
    DCHECK_EQ(length, writer.length());

    return frame;
  }

  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  MockQuicConnection* connection_;
  std::unique_ptr<TestSession> session_;

  // Owned by the |session_|.
  TestStream* stream_;
  TestStream* stream2_;

  SpdyHeaderBlock headers_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSpdyStreamTest,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicSpdyStreamTest, ProcessHeaderList) {
  Initialize(kShouldProcessData);

  stream_->OnStreamHeadersPriority(
      spdy::SpdyStreamPrecedence(kV3HighestPriority));
  ProcessHeaders(false, headers_);
  EXPECT_EQ("", stream_->data());
  EXPECT_FALSE(stream_->header_list().empty());
  EXPECT_FALSE(stream_->IsDoneReading());
}

TEST_P(QuicSpdyStreamTest, ProcessTooLargeHeaderList) {
  Initialize(kShouldProcessData);

  if (!UsesHttp3()) {
    QuicHeaderList headers;
    stream_->OnStreamHeadersPriority(
        spdy::SpdyStreamPrecedence(kV3HighestPriority));

    EXPECT_CALL(*session_,
                SendRstStream(stream_->id(), QUIC_HEADERS_TOO_LARGE, 0));
    stream_->OnStreamHeaderList(false, 1 << 20, headers);

    EXPECT_THAT(stream_->stream_error(), IsStreamError(QUIC_HEADERS_TOO_LARGE));

    return;
  }

  // Header list size includes 32 bytes for overhead per header field.
  session_->set_max_inbound_header_list_size(40);
  std::string headers =
      HeadersFrame({std::make_pair("foo", "too long headers")});

  QuicStreamFrame frame(stream_->id(), false, 0, headers);

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE,
                      MatchesRegex("Too large headers received on stream \\d+"),
                      _));

  stream_->OnStreamFrame(frame);

  EXPECT_TRUE(stream_->header_list().empty());
}

TEST_P(QuicSpdyStreamTest, ProcessHeaderListWithFin) {
  Initialize(kShouldProcessData);

  size_t total_bytes = 0;
  QuicHeaderList headers;
  for (auto p : headers_) {
    headers.OnHeader(p.first, p.second);
    total_bytes += p.first.size() + p.second.size();
  }
  stream_->OnStreamHeadersPriority(
      spdy::SpdyStreamPrecedence(kV3HighestPriority));
  stream_->OnStreamHeaderList(true, total_bytes, headers);
  EXPECT_EQ("", stream_->data());
  EXPECT_FALSE(stream_->header_list().empty());
  EXPECT_FALSE(stream_->IsDoneReading());
  EXPECT_TRUE(stream_->HasReceivedFinalOffset());
}

// A valid status code should be 3-digit integer. The first digit should be in
// the range of [1, 5]. All the others are invalid.
TEST_P(QuicSpdyStreamTest, ParseHeaderStatusCode) {
  Initialize(kShouldProcessData);
  int status_code = 0;

  // Valid status codes.
  headers_[":status"] = "404";
  EXPECT_TRUE(stream_->ParseHeaderStatusCode(headers_, &status_code));
  EXPECT_EQ(404, status_code);

  headers_[":status"] = "100";
  EXPECT_TRUE(stream_->ParseHeaderStatusCode(headers_, &status_code));
  EXPECT_EQ(100, status_code);

  headers_[":status"] = "599";
  EXPECT_TRUE(stream_->ParseHeaderStatusCode(headers_, &status_code));
  EXPECT_EQ(599, status_code);

  // Invalid status codes.
  headers_[":status"] = "010";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "600";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "200 ok";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "2000";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "+200";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "+20";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "-10";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "-100";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  // Leading or trailing spaces are also invalid.
  headers_[":status"] = " 200";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "200 ";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = " 200 ";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));

  headers_[":status"] = "  ";
  EXPECT_FALSE(stream_->ParseHeaderStatusCode(headers_, &status_code));
}

TEST_P(QuicSpdyStreamTest, MarkHeadersConsumed) {
  Initialize(kShouldProcessData);

  std::string body = "this is the body";
  QuicHeaderList headers = ProcessHeaders(false, headers_);
  EXPECT_EQ(headers, stream_->header_list());

  stream_->ConsumeHeaderList();
  EXPECT_EQ(QuicHeaderList(), stream_->header_list());
}

TEST_P(QuicSpdyStreamTest, ProcessWrongFramesOnSpdyStream) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  testing::InSequence s;
  connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
  GoAwayFrame goaway;
  goaway.stream_id = 0x1;
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeGoAwayFrame(goaway, &buffer);
  std::string data = std::string(buffer.get(), header_length);

  EXPECT_EQ("", stream_->data());
  QuicHeaderList headers = ProcessHeaders(false, headers_);
  EXPECT_EQ(headers, stream_->header_list());
  stream_->ConsumeHeaderList();
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_FRAME_UNEXPECTED_ON_SPDY_STREAM, _, _))
      .WillOnce(
          (Invoke([this](QuicErrorCode error, const std::string& error_details,
                         ConnectionCloseBehavior connection_close_behavior) {
            connection_->ReallyCloseConnection(error, error_details,
                                               connection_close_behavior);
          })));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(*session_, OnConnectionClosed(_, _))
      .WillOnce(Invoke([this](const QuicConnectionCloseFrame& frame,
                              ConnectionCloseSource source) {
        session_->ReallyOnConnectionClosed(frame, source);
      }));
  EXPECT_CALL(*session_, SendRstStream(_, _, _));
  EXPECT_CALL(*session_, SendRstStream(_, _, _));

  stream_->OnStreamFrame(frame);
}

TEST_P(QuicSpdyStreamTest, Http3FrameError) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // PUSH_PROMISE frame with empty payload is considered invalid.
  std::string invalid_http3_frame = quiche::QuicheTextUtils::HexDecode("0500");
  QuicStreamFrame stream_frame(stream_->id(), /* fin = */ false,
                               /* offset = */ 0, invalid_http3_frame);

  EXPECT_CALL(*connection_, CloseConnection(QUIC_HTTP_FRAME_ERROR, _, _));
  stream_->OnStreamFrame(stream_frame);
}

TEST_P(QuicSpdyStreamTest, UnexpectedHttp3Frame) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // SETTINGS frame with empty payload.
  std::string settings = quiche::QuicheTextUtils::HexDecode("0400");
  QuicStreamFrame stream_frame(stream_->id(), /* fin = */ false,
                               /* offset = */ 0, settings);

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_FRAME_UNEXPECTED_ON_SPDY_STREAM, _, _));
  stream_->OnStreamFrame(stream_frame);
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndBody) {
  Initialize(kShouldProcessData);

  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  EXPECT_EQ("", stream_->data());
  QuicHeaderList headers = ProcessHeaders(false, headers_);
  EXPECT_EQ(headers, stream_->header_list());
  stream_->ConsumeHeaderList();
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame);
  EXPECT_EQ(QuicHeaderList(), stream_->header_list());
  EXPECT_EQ(body, stream_->data());
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndBodyFragments) {
  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  for (size_t fragment_size = 1; fragment_size < data.size(); ++fragment_size) {
    Initialize(kShouldProcessData);
    QuicHeaderList headers = ProcessHeaders(false, headers_);
    ASSERT_EQ(headers, stream_->header_list());
    stream_->ConsumeHeaderList();
    for (size_t offset = 0; offset < data.size(); offset += fragment_size) {
      size_t remaining_data = data.size() - offset;
      quiche::QuicheStringPiece fragment(
          data.data() + offset, std::min(fragment_size, remaining_data));
      QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false,
                            offset, quiche::QuicheStringPiece(fragment));
      stream_->OnStreamFrame(frame);
    }
    ASSERT_EQ(body, stream_->data()) << "fragment_size: " << fragment_size;
  }
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndBodyFragmentsSplit) {
  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  for (size_t split_point = 1; split_point < data.size() - 1; ++split_point) {
    Initialize(kShouldProcessData);
    QuicHeaderList headers = ProcessHeaders(false, headers_);
    ASSERT_EQ(headers, stream_->header_list());
    stream_->ConsumeHeaderList();

    quiche::QuicheStringPiece fragment1(data.data(), split_point);
    QuicStreamFrame frame1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                           quiche::QuicheStringPiece(fragment1));
    stream_->OnStreamFrame(frame1);

    quiche::QuicheStringPiece fragment2(data.data() + split_point,
                                        data.size() - split_point);
    QuicStreamFrame frame2(GetNthClientInitiatedBidirectionalId(0), false,
                           split_point, quiche::QuicheStringPiece(fragment2));
    stream_->OnStreamFrame(frame2);

    ASSERT_EQ(body, stream_->data()) << "split_point: " << split_point;
  }
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndBodyReadv) {
  Initialize(!kShouldProcessData);

  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  ProcessHeaders(false, headers_);
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame);
  stream_->ConsumeHeaderList();

  char buffer[2048];
  ASSERT_LT(data.length(), QUICHE_ARRAYSIZE(buffer));
  struct iovec vec;
  vec.iov_base = buffer;
  vec.iov_len = QUICHE_ARRAYSIZE(buffer);

  size_t bytes_read = stream_->Readv(&vec, 1);
  QuicStreamPeer::CloseReadSide(stream_);
  EXPECT_EQ(body.length(), bytes_read);
  EXPECT_EQ(body, std::string(buffer, bytes_read));
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndLargeBodySmallReadv) {
  Initialize(kShouldProcessData);
  std::string body(12 * 1024, 'a');
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  ProcessHeaders(false, headers_);
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame);
  stream_->ConsumeHeaderList();
  char buffer[2048];
  char buffer2[2048];
  struct iovec vec[2];
  vec[0].iov_base = buffer;
  vec[0].iov_len = QUICHE_ARRAYSIZE(buffer);
  vec[1].iov_base = buffer2;
  vec[1].iov_len = QUICHE_ARRAYSIZE(buffer2);
  size_t bytes_read = stream_->Readv(vec, 2);
  EXPECT_EQ(2048u * 2, bytes_read);
  EXPECT_EQ(body.substr(0, 2048), std::string(buffer, 2048));
  EXPECT_EQ(body.substr(2048, 2048), std::string(buffer2, 2048));
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndBodyMarkConsumed) {
  Initialize(!kShouldProcessData);

  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  ProcessHeaders(false, headers_);
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame);
  stream_->ConsumeHeaderList();

  struct iovec vec;

  EXPECT_EQ(1, stream_->GetReadableRegions(&vec, 1));
  EXPECT_EQ(body.length(), vec.iov_len);
  EXPECT_EQ(body, std::string(static_cast<char*>(vec.iov_base), vec.iov_len));

  stream_->MarkConsumed(body.length());
  EXPECT_EQ(data.length(), stream_->flow_controller()->bytes_consumed());
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndConsumeMultipleBody) {
  Initialize(!kShouldProcessData);
  std::string body1 = "this is body 1";
  std::string data1 = UsesHttp3() ? DataFrame(body1) : body1;
  std::string body2 = "body 2";
  std::string data2 = UsesHttp3() ? DataFrame(body2) : body2;

  ProcessHeaders(false, headers_);
  QuicStreamFrame frame1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                         quiche::QuicheStringPiece(data1));
  QuicStreamFrame frame2(GetNthClientInitiatedBidirectionalId(0), false,
                         data1.length(), quiche::QuicheStringPiece(data2));
  stream_->OnStreamFrame(frame1);
  stream_->OnStreamFrame(frame2);
  stream_->ConsumeHeaderList();

  stream_->MarkConsumed(body1.length() + body2.length());
  EXPECT_EQ(data1.length() + data2.length(),
            stream_->flow_controller()->bytes_consumed());
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersAndBodyIncrementalReadv) {
  Initialize(!kShouldProcessData);

  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  ProcessHeaders(false, headers_);
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame);
  stream_->ConsumeHeaderList();

  char buffer[1];
  struct iovec vec;
  vec.iov_base = buffer;
  vec.iov_len = QUICHE_ARRAYSIZE(buffer);

  for (size_t i = 0; i < body.length(); ++i) {
    size_t bytes_read = stream_->Readv(&vec, 1);
    ASSERT_EQ(1u, bytes_read);
    EXPECT_EQ(body.data()[i], buffer[0]);
  }
}

TEST_P(QuicSpdyStreamTest, ProcessHeadersUsingReadvWithMultipleIovecs) {
  Initialize(!kShouldProcessData);

  std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  ProcessHeaders(false, headers_);
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame);
  stream_->ConsumeHeaderList();

  char buffer1[1];
  char buffer2[1];
  struct iovec vec[2];
  vec[0].iov_base = buffer1;
  vec[0].iov_len = QUICHE_ARRAYSIZE(buffer1);
  vec[1].iov_base = buffer2;
  vec[1].iov_len = QUICHE_ARRAYSIZE(buffer2);

  for (size_t i = 0; i < body.length(); i += 2) {
    size_t bytes_read = stream_->Readv(vec, 2);
    ASSERT_EQ(2u, bytes_read) << i;
    ASSERT_EQ(body.data()[i], buffer1[0]) << i;
    ASSERT_EQ(body.data()[i + 1], buffer2[0]) << i;
  }
}

// Tests that we send a BLOCKED frame to the peer when we attempt to write, but
// are flow control blocked.
TEST_P(QuicSpdyStreamTest, StreamFlowControlBlocked) {
  Initialize(kShouldProcessData);
  testing::InSequence seq;

  // Set a small flow control limit.
  const uint64_t kWindow = 36;
  QuicFlowControllerPeer::SetSendWindowOffset(stream_->flow_controller(),
                                              kWindow);
  EXPECT_EQ(kWindow, QuicFlowControllerPeer::SendWindowOffset(
                         stream_->flow_controller()));

  // Try to send more data than the flow control limit allows.
  const uint64_t kOverflow = 15;
  std::string body(kWindow + kOverflow, 'a');

  const uint64_t kHeaderLength = UsesHttp3() ? 2 : 0;
  if (UsesHttp3()) {
    EXPECT_CALL(*session_, WritevData(_, kHeaderLength, _, NO_FIN, _, _));
  }
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _))
      .WillOnce(Return(QuicConsumedData(kWindow - kHeaderLength, true)));
  EXPECT_CALL(*connection_, SendControlFrame(_));
  stream_->WriteOrBufferBody(body, false);

  // Should have sent as much as possible, resulting in no send window left.
  EXPECT_EQ(0u,
            QuicFlowControllerPeer::SendWindowSize(stream_->flow_controller()));

  // And we should have queued the overflowed data.
  EXPECT_EQ(kOverflow + kHeaderLength, stream_->BufferedDataBytes());
}

// The flow control receive window decreases whenever we add new bytes to the
// sequencer, whether they are consumed immediately or buffered. However we only
// send WINDOW_UPDATE frames based on increasing number of bytes consumed.
TEST_P(QuicSpdyStreamTest, StreamFlowControlNoWindowUpdateIfNotConsumed) {
  // Don't process data - it will be buffered instead.
  Initialize(!kShouldProcessData);

  // Expect no WINDOW_UPDATE frames to be sent.
  EXPECT_CALL(*connection_, SendWindowUpdate(_, _)).Times(0);

  // Set a small flow control receive window.
  const uint64_t kWindow = 36;
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream_->flow_controller(),
                                                 kWindow);
  QuicFlowControllerPeer::SetMaxReceiveWindow(stream_->flow_controller(),
                                              kWindow);
  EXPECT_EQ(kWindow, QuicFlowControllerPeer::ReceiveWindowOffset(
                         stream_->flow_controller()));

  // Stream receives enough data to fill a fraction of the receive window.
  std::string body(kWindow / 3, 'a');
  QuicByteCount header_length = 0;
  std::string data;

  if (UsesHttp3()) {
    std::unique_ptr<char[]> buffer;
    header_length =
        HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);
    std::string header = std::string(buffer.get(), header_length);
    data = header + body;
  } else {
    data = body;
  }

  ProcessHeaders(false, headers_);

  QuicStreamFrame frame1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                         quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame1);
  EXPECT_EQ(
      kWindow - (kWindow / 3) - header_length,
      QuicFlowControllerPeer::ReceiveWindowSize(stream_->flow_controller()));

  // Now receive another frame which results in the receive window being over
  // half full. This should all be buffered, decreasing the receive window but
  // not sending WINDOW_UPDATE.
  QuicStreamFrame frame2(GetNthClientInitiatedBidirectionalId(0), false,
                         kWindow / 3 + header_length,
                         quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame2);
  EXPECT_EQ(
      kWindow - (2 * kWindow / 3) - 2 * header_length,
      QuicFlowControllerPeer::ReceiveWindowSize(stream_->flow_controller()));
}

// Tests that on receipt of data, the stream updates its receive window offset
// appropriately, and sends WINDOW_UPDATE frames when its receive window drops
// too low.
TEST_P(QuicSpdyStreamTest, StreamFlowControlWindowUpdate) {
  Initialize(kShouldProcessData);

  // Set a small flow control limit.
  const uint64_t kWindow = 36;
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream_->flow_controller(),
                                                 kWindow);
  QuicFlowControllerPeer::SetMaxReceiveWindow(stream_->flow_controller(),
                                              kWindow);
  EXPECT_EQ(kWindow, QuicFlowControllerPeer::ReceiveWindowOffset(
                         stream_->flow_controller()));

  // Stream receives enough data to fill a fraction of the receive window.
  std::string body(kWindow / 3, 'a');
  QuicByteCount header_length = 0;
  std::string data;

  if (UsesHttp3()) {
    std::unique_ptr<char[]> buffer;
    header_length =
        HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);
    std::string header = std::string(buffer.get(), header_length);
    data = header + body;
  } else {
    data = body;
  }

  ProcessHeaders(false, headers_);
  stream_->ConsumeHeaderList();

  QuicStreamFrame frame1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                         quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame1);
  EXPECT_EQ(
      kWindow - (kWindow / 3) - header_length,
      QuicFlowControllerPeer::ReceiveWindowSize(stream_->flow_controller()));

  // Now receive another frame which results in the receive window being over
  // half full.  This will trigger the stream to increase its receive window
  // offset and send a WINDOW_UPDATE. The result will be again an available
  // window of kWindow bytes.
  QuicStreamFrame frame2(GetNthClientInitiatedBidirectionalId(0), false,
                         kWindow / 3 + header_length,
                         quiche::QuicheStringPiece(data));
  EXPECT_CALL(*connection_, SendControlFrame(_));
  stream_->OnStreamFrame(frame2);
  EXPECT_EQ(kWindow, QuicFlowControllerPeer::ReceiveWindowSize(
                         stream_->flow_controller()));
}

// Tests that on receipt of data, the connection updates its receive window
// offset appropriately, and sends WINDOW_UPDATE frames when its receive window
// drops too low.
TEST_P(QuicSpdyStreamTest, ConnectionFlowControlWindowUpdate) {
  Initialize(kShouldProcessData);

  // Set a small flow control limit for streams and connection.
  const uint64_t kWindow = 36;
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream_->flow_controller(),
                                                 kWindow);
  QuicFlowControllerPeer::SetMaxReceiveWindow(stream_->flow_controller(),
                                              kWindow);
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream2_->flow_controller(),
                                                 kWindow);
  QuicFlowControllerPeer::SetMaxReceiveWindow(stream2_->flow_controller(),
                                              kWindow);
  QuicFlowControllerPeer::SetReceiveWindowOffset(session_->flow_controller(),
                                                 kWindow);
  QuicFlowControllerPeer::SetMaxReceiveWindow(session_->flow_controller(),
                                              kWindow);

  // Supply headers to both streams so that they are happy to receive data.
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                              headers);
  stream_->ConsumeHeaderList();
  stream2_->OnStreamHeaderList(false, headers.uncompressed_header_bytes(),
                               headers);
  stream2_->ConsumeHeaderList();

  // Each stream gets a quarter window of data. This should not trigger a
  // WINDOW_UPDATE for either stream, nor for the connection.
  QuicByteCount header_length = 0;
  std::string body;
  std::string data;
  std::string data2;
  std::string body2(1, 'a');

  if (UsesHttp3()) {
    body = std::string(kWindow / 4 - 2, 'a');
    std::unique_ptr<char[]> buffer;
    header_length =
        HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);
    std::string header = std::string(buffer.get(), header_length);
    data = header + body;
    std::unique_ptr<char[]> buffer2;
    QuicByteCount header_length2 =
        HttpEncoder::SerializeDataFrameHeader(body2.length(), &buffer2);
    std::string header2 = std::string(buffer2.get(), header_length2);
    data2 = header2 + body2;
  } else {
    body = std::string(kWindow / 4, 'a');
    data = body;
    data2 = body2;
  }

  QuicStreamFrame frame1(GetNthClientInitiatedBidirectionalId(0), false, 0,
                         quiche::QuicheStringPiece(data));
  stream_->OnStreamFrame(frame1);
  QuicStreamFrame frame2(GetNthClientInitiatedBidirectionalId(1), false, 0,
                         quiche::QuicheStringPiece(data));
  stream2_->OnStreamFrame(frame2);

  // Now receive a further single byte on one stream - again this does not
  // trigger a stream WINDOW_UPDATE, but now the connection flow control window
  // is over half full and thus a connection WINDOW_UPDATE is sent.
  EXPECT_CALL(*connection_, SendControlFrame(_));
  QuicStreamFrame frame3(GetNthClientInitiatedBidirectionalId(0), false,
                         body.length() + header_length,
                         quiche::QuicheStringPiece(data2));
  stream_->OnStreamFrame(frame3);
}

// Tests that on if the peer sends too much data (i.e. violates the flow control
// protocol), then we terminate the connection.
TEST_P(QuicSpdyStreamTest, StreamFlowControlViolation) {
  // Stream should not process data, so that data gets buffered in the
  // sequencer, triggering flow control limits.
  Initialize(!kShouldProcessData);

  // Set a small flow control limit.
  const uint64_t kWindow = 50;
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream_->flow_controller(),
                                                 kWindow);

  ProcessHeaders(false, headers_);

  // Receive data to overflow the window, violating flow control.
  std::string body(kWindow + 1, 'a');
  std::string data = UsesHttp3() ? DataFrame(body) : body;
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA, _, _));
  stream_->OnStreamFrame(frame);
}

TEST_P(QuicSpdyStreamTest, TestHandlingQuicRstStreamNoError) {
  Initialize(kShouldProcessData);
  ProcessHeaders(false, headers_);

  stream_->OnStreamReset(QuicRstStreamFrame(
      kInvalidControlFrameId, stream_->id(), QUIC_STREAM_NO_ERROR, 0));
  EXPECT_TRUE(stream_->write_side_closed());
  EXPECT_FALSE(stream_->reading_stopped());
}

// Tests that on if the peer sends too much data (i.e. violates the flow control
// protocol), at the connection level (rather than the stream level) then we
// terminate the connection.
TEST_P(QuicSpdyStreamTest, ConnectionFlowControlViolation) {
  // Stream should not process data, so that data gets buffered in the
  // sequencer, triggering flow control limits.
  Initialize(!kShouldProcessData);

  // Set a small flow control window on streams, and connection.
  const uint64_t kStreamWindow = 50;
  const uint64_t kConnectionWindow = 10;
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream_->flow_controller(),
                                                 kStreamWindow);
  QuicFlowControllerPeer::SetReceiveWindowOffset(session_->flow_controller(),
                                                 kConnectionWindow);

  ProcessHeaders(false, headers_);

  // Send enough data to overflow the connection level flow control window.
  std::string body(kConnectionWindow + 1, 'a');
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  EXPECT_LT(data.size(), kStreamWindow);
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), false, 0,
                        quiche::QuicheStringPiece(data));

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA, _, _));
  stream_->OnStreamFrame(frame);
}

// An attempt to write a FIN with no data should not be flow control blocked,
// even if the send window is 0.
TEST_P(QuicSpdyStreamTest, StreamFlowControlFinNotBlocked) {
  Initialize(kShouldProcessData);

  // Set a flow control limit of zero.
  QuicFlowControllerPeer::SetReceiveWindowOffset(stream_->flow_controller(), 0);
  EXPECT_EQ(0u, QuicFlowControllerPeer::ReceiveWindowOffset(
                    stream_->flow_controller()));

  // Send a frame with a FIN but no data. This should not be blocked.
  std::string body = "";
  bool fin = true;

  EXPECT_CALL(*connection_,
              SendBlocked(GetNthClientInitiatedBidirectionalId(0)))
      .Times(0);
  EXPECT_CALL(*session_, WritevData(_, 0, _, FIN, _, _));

  stream_->WriteOrBufferBody(body, fin);
}

// Test that receiving trailing headers from the peer via OnStreamHeaderList()
// works, and can be read from the stream and consumed.
TEST_P(QuicSpdyStreamTest, ReceivingTrailersViaHeaderList) {
  Initialize(kShouldProcessData);

  // Receive initial headers.
  size_t total_bytes = 0;
  QuicHeaderList headers;
  for (const auto& p : headers_) {
    headers.OnHeader(p.first, p.second);
    total_bytes += p.first.size() + p.second.size();
  }

  stream_->OnStreamHeadersPriority(
      spdy::SpdyStreamPrecedence(kV3HighestPriority));
  stream_->OnStreamHeaderList(/*fin=*/false, total_bytes, headers);
  stream_->ConsumeHeaderList();

  // Receive trailing headers.
  SpdyHeaderBlock trailers_block;
  trailers_block["key1"] = "value1";
  trailers_block["key2"] = "value2";
  trailers_block["key3"] = "value3";
  SpdyHeaderBlock trailers_block_with_final_offset = trailers_block.Clone();
  if (!UsesHttp3()) {
    // :final-offset pseudo-header is only added if trailers are sent
    // on the headers stream.
    trailers_block_with_final_offset[kFinalOffsetHeaderKey] = "0";
  }
  total_bytes = 0;
  QuicHeaderList trailers;
  for (const auto& p : trailers_block_with_final_offset) {
    trailers.OnHeader(p.first, p.second);
    total_bytes += p.first.size() + p.second.size();
  }
  stream_->OnStreamHeaderList(/*fin=*/true, total_bytes, trailers);

  // The trailers should be decompressed, and readable from the stream.
  EXPECT_TRUE(stream_->trailers_decompressed());
  EXPECT_EQ(trailers_block, stream_->received_trailers());

  // IsDoneReading() returns false until trailers marked consumed.
  EXPECT_FALSE(stream_->IsDoneReading());
  stream_->MarkTrailersConsumed();
  EXPECT_TRUE(stream_->IsDoneReading());
}

// Test that when receiving trailing headers with an offset before response
// body, stream is closed at the right offset.
TEST_P(QuicSpdyStreamTest, ReceivingTrailersWithOffset) {
  // kFinalOffsetHeaderKey is not used when HEADERS are sent on the
  // request/response stream.
  if (UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Receive initial headers.
  QuicHeaderList headers = ProcessHeaders(false, headers_);
  stream_->ConsumeHeaderList();

  const std::string body = "this is the body";
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  // Receive trailing headers.
  SpdyHeaderBlock trailers_block;
  trailers_block["key1"] = "value1";
  trailers_block["key2"] = "value2";
  trailers_block["key3"] = "value3";
  trailers_block[kFinalOffsetHeaderKey] =
      quiche::QuicheTextUtils::Uint64ToString(data.size());

  QuicHeaderList trailers = ProcessHeaders(true, trailers_block);

  // The trailers should be decompressed, and readable from the stream.
  EXPECT_TRUE(stream_->trailers_decompressed());

  // The final offset trailer will be consumed by QUIC.
  trailers_block.erase(kFinalOffsetHeaderKey);
  EXPECT_EQ(trailers_block, stream_->received_trailers());

  // Consuming the trailers erases them from the stream.
  stream_->MarkTrailersConsumed();
  EXPECT_TRUE(stream_->FinishedReadingTrailers());

  EXPECT_FALSE(stream_->IsDoneReading());
  // Receive and consume body.
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), /*fin=*/false,
                        0, data);
  stream_->OnStreamFrame(frame);
  EXPECT_EQ(body, stream_->data());
  EXPECT_TRUE(stream_->IsDoneReading());
}

// Test that receiving trailers without a final offset field is an error.
TEST_P(QuicSpdyStreamTest, ReceivingTrailersWithoutOffset) {
  // kFinalOffsetHeaderKey is not used when HEADERS are sent on the
  // request/response stream.
  if (UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Receive initial headers.
  ProcessHeaders(false, headers_);
  stream_->ConsumeHeaderList();

  // Receive trailing headers, without kFinalOffsetHeaderKey.
  SpdyHeaderBlock trailers_block;
  trailers_block["key1"] = "value1";
  trailers_block["key2"] = "value2";
  trailers_block["key3"] = "value3";
  auto trailers = AsHeaderList(trailers_block);

  // Verify that the trailers block didn't contain a final offset.
  EXPECT_EQ("", trailers_block[kFinalOffsetHeaderKey].as_string());

  // Receipt of the malformed trailers will close the connection.
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_HEADERS_STREAM_DATA, _, _))
      .Times(1);
  stream_->OnStreamHeaderList(/*fin=*/true,
                              trailers.uncompressed_header_bytes(), trailers);
}

// Test that received Trailers must always have the FIN set.
TEST_P(QuicSpdyStreamTest, ReceivingTrailersWithoutFin) {
  // In IETF QUIC, there is no such thing as FIN flag on HTTP/3 frames like the
  // HEADERS frame.
  if (UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Receive initial headers.
  auto headers = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(/*fin=*/false,
                              headers.uncompressed_header_bytes(), headers);
  stream_->ConsumeHeaderList();

  // Receive trailing headers with FIN deliberately set to false.
  SpdyHeaderBlock trailers_block;
  trailers_block["foo"] = "bar";
  auto trailers = AsHeaderList(trailers_block);

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_HEADERS_STREAM_DATA, _, _))
      .Times(1);
  stream_->OnStreamHeaderList(/*fin=*/false,
                              trailers.uncompressed_header_bytes(), trailers);
}

TEST_P(QuicSpdyStreamTest, ReceivingTrailersAfterHeadersWithFin) {
  // If headers are received with a FIN, no trailers should then arrive.
  Initialize(kShouldProcessData);

  // If HEADERS frames are sent on the request/response stream, then the
  // sequencer will signal an error if any stream data arrives after a FIN,
  // so QuicSpdyStream does not need to.
  if (UsesHttp3()) {
    return;
  }

  // Receive initial headers with FIN set.
  ProcessHeaders(true, headers_);
  stream_->ConsumeHeaderList();

  // Receive trailing headers after FIN already received.
  SpdyHeaderBlock trailers_block;
  trailers_block["foo"] = "bar";
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_HEADERS_STREAM_DATA, _, _))
      .Times(1);
  ProcessHeaders(true, trailers_block);
}

// If body data are received with a FIN, no trailers should then arrive.
TEST_P(QuicSpdyStreamTest, ReceivingTrailersAfterBodyWithFin) {
  // If HEADERS frames are sent on the request/response stream,
  // then the sequencer will block them from reaching QuicSpdyStream
  // after the stream is closed.
  if (UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Receive initial headers without FIN set.
  ProcessHeaders(false, headers_);
  stream_->ConsumeHeaderList();

  // Receive body data, with FIN.
  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), /*fin=*/true,
                        0, "body");
  stream_->OnStreamFrame(frame);

  // Receive trailing headers after FIN already received.
  SpdyHeaderBlock trailers_block;
  trailers_block["foo"] = "bar";
  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_INVALID_HEADERS_STREAM_DATA, _, _))
      .Times(1);
  ProcessHeaders(true, trailers_block);
}

TEST_P(QuicSpdyStreamTest, ClosingStreamWithNoTrailers) {
  // Verify that a stream receiving headers, body, and no trailers is correctly
  // marked as done reading on consumption of headers and body.
  Initialize(kShouldProcessData);

  // Receive and consume initial headers with FIN not set.
  auto h = AsHeaderList(headers_);
  stream_->OnStreamHeaderList(/*fin=*/false, h.uncompressed_header_bytes(), h);
  stream_->ConsumeHeaderList();

  // Receive and consume body with FIN set, and no trailers.
  std::string body(1024, 'x');
  std::string data = UsesHttp3() ? DataFrame(body) : body;

  QuicStreamFrame frame(GetNthClientInitiatedBidirectionalId(0), /*fin=*/true,
                        0, data);
  stream_->OnStreamFrame(frame);

  EXPECT_TRUE(stream_->IsDoneReading());
}

// Test that writing trailers will send a FIN, as Trailers are the last thing to
// be sent on a stream.
TEST_P(QuicSpdyStreamTest, WritingTrailersSendsAFin) {
  Initialize(kShouldProcessData);

  if (UsesHttp3()) {
    // In this case, TestStream::WriteHeadersImpl() does not prevent writes.
    // Four writes on the request stream: HEADERS frame header and payload both
    // for headers and trailers.
    EXPECT_CALL(*session_, WritevData(stream_->id(), _, _, _, _, _)).Times(4);
  }

  // Write the initial headers, without a FIN.
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/false, nullptr);

  // Writing trailers implicitly sends a FIN.
  SpdyHeaderBlock trailers;
  trailers["trailer key"] = "trailer value";
  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  stream_->WriteTrailers(std::move(trailers), nullptr);
  EXPECT_TRUE(stream_->fin_sent());
}

TEST_P(QuicSpdyStreamTest, DoNotSendPriorityUpdateWithDefaultUrgency) {
  if (!UsesHttp3()) {
    return;
  }

  InitializeWithPerspective(kShouldProcessData, Perspective::IS_CLIENT);
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  // Four writes on the request stream: HEADERS frame header and payload both
  // for headers and trailers.
  EXPECT_CALL(*session_, WritevData(stream_->id(), _, _, _, _, _)).Times(4);

  // No PRIORITY_UPDATE frames on the control stream,
  // because the stream has default priority.
  auto send_control_stream =
      QuicSpdySessionPeer::GetSendControlStream(session_.get());
  EXPECT_CALL(*session_, WritevData(send_control_stream->id(), _, _, _, _, _))
      .Times(0);

  // Write the initial headers, without a FIN.
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  EXPECT_CALL(debug_visitor, OnHeadersFrameSent(stream_->id(), _));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/false, nullptr);

  // Writing trailers implicitly sends a FIN.
  SpdyHeaderBlock trailers;
  trailers["trailer key"] = "trailer value";
  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  EXPECT_CALL(debug_visitor, OnHeadersFrameSent(stream_->id(), _));
  stream_->WriteTrailers(std::move(trailers), nullptr);
  EXPECT_TRUE(stream_->fin_sent());
}

TEST_P(QuicSpdyStreamTest, ChangePriority) {
  if (!UsesHttp3()) {
    return;
  }

  InitializeWithPerspective(kShouldProcessData, Perspective::IS_CLIENT);
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  // Two writes on the request stream: HEADERS frame header and payload.
  EXPECT_CALL(*session_, WritevData(stream_->id(), _, _, _, _, _)).Times(2);
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  EXPECT_CALL(debug_visitor, OnHeadersFrameSent(stream_->id(), _));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/false, nullptr);

  // PRIORITY_UPDATE frame on the control stream.
  auto send_control_stream =
      QuicSpdySessionPeer::GetSendControlStream(session_.get());
  EXPECT_CALL(*session_, WritevData(send_control_stream->id(), _, _, _, _, _));
  PriorityUpdateFrame priority_update;
  priority_update.prioritized_element_id = 0;
  priority_update.priority_field_value = "u=0";
  EXPECT_CALL(debug_visitor, OnPriorityUpdateFrameSent(priority_update));
  stream_->SetPriority(spdy::SpdyStreamPrecedence(kV3HighestPriority));
}

TEST_P(QuicSpdyStreamTest, ChangePriorityBeforeWritingHeaders) {
  if (!UsesHttp3()) {
    return;
  }

  InitializeWithPerspective(kShouldProcessData, Perspective::IS_CLIENT);

  // PRIORITY_UPDATE frame sent on the control stream as soon as SetPriority()
  // is called, before HEADERS frame is sent.
  auto send_control_stream =
      QuicSpdySessionPeer::GetSendControlStream(session_.get());
  EXPECT_CALL(*session_, WritevData(send_control_stream->id(), _, _, _, _, _));

  stream_->SetPriority(spdy::SpdyStreamPrecedence(kV3HighestPriority));
  testing::Mock::VerifyAndClearExpectations(session_.get());

  // Two writes on the request stream: HEADERS frame header and payload.
  // PRIORITY_UPDATE frame is not sent this time, because one is already sent.
  EXPECT_CALL(*session_, WritevData(stream_->id(), _, _, _, _, _)).Times(2);
  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/true, nullptr);
}

// Test that when writing trailers, the trailers that are actually sent to the
// peer contain the final offset field indicating last byte of data.
TEST_P(QuicSpdyStreamTest, WritingTrailersFinalOffset) {
  Initialize(kShouldProcessData);

  if (UsesHttp3()) {
    // In this case, TestStream::WriteHeadersImpl() does not prevent writes.
    // HEADERS frame header and payload on the request stream.
    EXPECT_CALL(*session_, WritevData(stream_->id(), _, _, _, _, _)).Times(2);
  }

  // Write the initial headers.
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/false, nullptr);

  // Write non-zero body data to force a non-zero final offset.
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  std::string body(1024, 'x');  // 1 kB
  QuicByteCount header_length = 0;
  if (UsesHttp3()) {
    std::unique_ptr<char[]> buf;
    header_length = HttpEncoder::SerializeDataFrameHeader(body.length(), &buf);
  }

  stream_->WriteOrBufferBody(body, false);

  // The final offset field in the trailing headers is populated with the
  // number of body bytes written (including queued bytes).
  SpdyHeaderBlock trailers;
  trailers["trailer key"] = "trailer value";

  SpdyHeaderBlock expected_trailers(trailers.Clone());
  // :final-offset pseudo-header is only added if trailers are sent
  // on the headers stream.
  if (!UsesHttp3()) {
    expected_trailers[kFinalOffsetHeaderKey] =
        quiche::QuicheTextUtils::Uint64ToString(body.length() + header_length);
  }

  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  stream_->WriteTrailers(std::move(trailers), nullptr);
  EXPECT_EQ(expected_trailers, stream_->saved_headers());
}

// Test that if trailers are written after all other data has been written
// (headers and body), that this closes the stream for writing.
TEST_P(QuicSpdyStreamTest, WritingTrailersClosesWriteSide) {
  Initialize(kShouldProcessData);

  // Expect data being written on the stream.  In addition to that, headers are
  // also written on the stream in case of IETF QUIC.
  EXPECT_CALL(*session_, WritevData(stream_->id(), _, _, _, _, _))
      .Times(AtLeast(1));

  // Write the initial headers.
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/false, nullptr);

  // Write non-zero body data.
  const int kBodySize = 1 * 1024;  // 1 kB
  stream_->WriteOrBufferBody(std::string(kBodySize, 'x'), false);
  EXPECT_EQ(0u, stream_->BufferedDataBytes());

  // Headers and body have been fully written, there is no queued data. Writing
  // trailers marks the end of this stream, and thus the write side is closed.
  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  stream_->WriteTrailers(SpdyHeaderBlock(), nullptr);
  EXPECT_TRUE(stream_->write_side_closed());
}

// Test that the stream is not closed for writing when trailers are sent while
// there are still body bytes queued.
TEST_P(QuicSpdyStreamTest, WritingTrailersWithQueuedBytes) {
  // This test exercises sending trailers on the headers stream while data is
  // still queued on the response/request stream.  In IETF QUIC, data and
  // trailers are sent on the same stream, so this test does not apply.
  if (UsesHttp3()) {
    return;
  }

  testing::InSequence seq;
  Initialize(kShouldProcessData);

  // Write the initial headers.
  EXPECT_CALL(*stream_, WriteHeadersMock(false));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/false, nullptr);

  // Write non-zero body data, but only consume partially, ensuring queueing.
  const int kBodySize = 1 * 1024;  // 1 kB
  if (UsesHttp3()) {
    EXPECT_CALL(*session_, WritevData(_, 3, _, NO_FIN, _, _));
  }
  EXPECT_CALL(*session_, WritevData(_, kBodySize, _, NO_FIN, _, _))
      .WillOnce(Return(QuicConsumedData(kBodySize - 1, false)));
  stream_->WriteOrBufferBody(std::string(kBodySize, 'x'), false);
  EXPECT_EQ(1u, stream_->BufferedDataBytes());

  // Writing trailers will send a FIN, but not close the write side of the
  // stream as there are queued bytes.
  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  stream_->WriteTrailers(SpdyHeaderBlock(), nullptr);
  EXPECT_TRUE(stream_->fin_sent());
  EXPECT_FALSE(stream_->write_side_closed());

  // Writing the queued bytes will close the write side of the stream.
  EXPECT_CALL(*session_, WritevData(_, 1, _, NO_FIN, _, _));
  stream_->OnCanWrite();
  EXPECT_TRUE(stream_->write_side_closed());
}

// Test that it is not possible to write Trailers after a FIN has been sent.
TEST_P(QuicSpdyStreamTest, WritingTrailersAfterFIN) {
  // In IETF QUIC, there is no such thing as FIN flag on HTTP/3 frames like the
  // HEADERS frame.
  if (UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Write the initial headers, with a FIN.
  EXPECT_CALL(*stream_, WriteHeadersMock(true));
  stream_->WriteHeaders(SpdyHeaderBlock(), /*fin=*/true, nullptr);
  EXPECT_TRUE(stream_->fin_sent());

  // Writing Trailers should fail, as the FIN has already been sent.
  // populated with the number of body bytes written.
  EXPECT_QUIC_BUG(stream_->WriteTrailers(SpdyHeaderBlock(), nullptr),
                  "Trailers cannot be sent after a FIN");
}

TEST_P(QuicSpdyStreamTest, HeaderStreamNotiferCorrespondingSpdyStream) {
  // There is no headers stream if QPACK is used.
  if (UsesHttp3()) {
    return;
  }

  const char kHeader1[] = "Header1";
  const char kHeader2[] = "Header2";
  const char kBody1[] = "Test1";
  const char kBody2[] = "Test2";

  Initialize(kShouldProcessData);
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  testing::InSequence s;
  QuicReferenceCountedPointer<MockAckListener> ack_listener1(
      new MockAckListener());
  QuicReferenceCountedPointer<MockAckListener> ack_listener2(
      new MockAckListener());
  stream_->set_ack_listener(ack_listener1);
  stream2_->set_ack_listener(ack_listener2);

  session_->headers_stream()->WriteOrBufferData(kHeader1, false, ack_listener1);
  stream_->WriteOrBufferBody(kBody1, true);

  session_->headers_stream()->WriteOrBufferData(kHeader2, false, ack_listener2);
  stream2_->WriteOrBufferBody(kBody2, false);

  QuicStreamFrame frame1(
      QuicUtils::GetHeadersStreamId(connection_->transport_version()), false, 0,
      kHeader1);

  std::string data1 = UsesHttp3() ? DataFrame(kBody1) : kBody1;
  QuicStreamFrame frame2(stream_->id(), true, 0, data1);
  QuicStreamFrame frame3(
      QuicUtils::GetHeadersStreamId(connection_->transport_version()), false, 7,
      kHeader2);
  std::string data2 = UsesHttp3() ? DataFrame(kBody2) : kBody2;
  QuicStreamFrame frame4(stream2_->id(), false, 0, data2);

  EXPECT_CALL(*ack_listener1, OnPacketRetransmitted(7));
  session_->OnStreamFrameRetransmitted(frame1);

  EXPECT_CALL(*ack_listener1, OnPacketAcked(7, _));
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame1), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));
  EXPECT_CALL(*ack_listener1, OnPacketAcked(5, _));
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame2), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));
  EXPECT_CALL(*ack_listener2, OnPacketAcked(7, _));
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame3), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));
  EXPECT_CALL(*ack_listener2, OnPacketAcked(5, _));
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame4), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));
}

TEST_P(QuicSpdyStreamTest, StreamBecomesZombieWithWriteThatCloses) {
  Initialize(kShouldProcessData);
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  QuicStreamPeer::CloseReadSide(stream_);
  // This write causes stream to be closed.
  stream_->WriteOrBufferBody("Test1", true);
  // stream_ has unacked data and should become zombie.
  EXPECT_TRUE(QuicContainsKey(QuicSessionPeer::zombie_streams(session_.get()),
                              stream_->id()));
  EXPECT_TRUE(QuicSessionPeer::closed_streams(session_.get()).empty());
}

TEST_P(QuicSpdyStreamTest, OnPriorityFrame) {
  Initialize(kShouldProcessData);
  stream_->OnPriorityFrame(spdy::SpdyStreamPrecedence(kV3HighestPriority));
  EXPECT_EQ(spdy::SpdyStreamPrecedence(kV3HighestPriority),
            stream_->precedence());
}

TEST_P(QuicSpdyStreamTest, OnPriorityFrameAfterSendingData) {
  Initialize(kShouldProcessData);
  testing::InSequence seq;

  if (UsesHttp3()) {
    EXPECT_CALL(*session_, WritevData(_, 2, _, NO_FIN, _, _));
  }
  EXPECT_CALL(*session_, WritevData(_, 4, _, FIN, _, _));
  stream_->WriteOrBufferBody("data", true);
  stream_->OnPriorityFrame(spdy::SpdyStreamPrecedence(kV3HighestPriority));
  EXPECT_EQ(spdy::SpdyStreamPrecedence(kV3HighestPriority),
            stream_->precedence());
}

TEST_P(QuicSpdyStreamTest, SetPriorityBeforeUpdateStreamPriority) {
  MockQuicConnection* connection = new StrictMock<MockQuicConnection>(
      &helper_, &alarm_factory_, Perspective::IS_SERVER,
      SupportedVersions(GetParam()));
  std::unique_ptr<TestMockUpdateStreamSession> session(
      new StrictMock<TestMockUpdateStreamSession>(connection));
  auto stream =
      new StrictMock<TestStream>(GetNthClientInitiatedBidirectionalStreamId(
                                     session->transport_version(), 0),
                                 session.get(),
                                 /*should_process_data=*/true);
  session->ActivateStream(QuicWrapUnique(stream));

  // QuicSpdyStream::SetPriority() should eventually call UpdateStreamPriority()
  // on the session. Make sure stream->priority() returns the updated priority
  // if called within UpdateStreamPriority(). This expectation is enforced in
  // TestMockUpdateStreamSession::UpdateStreamPriority().
  session->SetExpectedStream(stream);
  session->SetExpectedPriority(spdy::SpdyStreamPrecedence(kV3HighestPriority));
  stream->SetPriority(spdy::SpdyStreamPrecedence(kV3HighestPriority));

  session->SetExpectedPriority(spdy::SpdyStreamPrecedence(kV3LowestPriority));
  stream->SetPriority(spdy::SpdyStreamPrecedence(kV3LowestPriority));
}

TEST_P(QuicSpdyStreamTest, StreamWaitsForAcks) {
  Initialize(kShouldProcessData);
  QuicReferenceCountedPointer<MockAckListener> mock_ack_listener(
      new StrictMock<MockAckListener>);
  stream_->set_ack_listener(mock_ack_listener);
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  // Stream is not waiting for acks initially.
  EXPECT_FALSE(stream_->IsWaitingForAcks());
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());

  // Send kData1.
  stream_->WriteOrBufferData("FooAndBar", false, nullptr);
  EXPECT_EQ(1u, QuicStreamPeer::SendBuffer(stream_).size());
  EXPECT_TRUE(stream_->IsWaitingForAcks());
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(9, _));
  QuicByteCount newly_acked_length = 0;
  EXPECT_TRUE(stream_->OnStreamFrameAcked(0, 9, false, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  // Stream is not waiting for acks as all sent data is acked.
  EXPECT_FALSE(stream_->IsWaitingForAcks());
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());

  // Send kData2.
  stream_->WriteOrBufferData("FooAndBar", false, nullptr);
  EXPECT_TRUE(stream_->IsWaitingForAcks());
  EXPECT_EQ(1u, QuicStreamPeer::SendBuffer(stream_).size());
  // Send FIN.
  stream_->WriteOrBufferData("", true, nullptr);
  // Fin only frame is not stored in send buffer.
  EXPECT_EQ(1u, QuicStreamPeer::SendBuffer(stream_).size());

  // kData2 is retransmitted.
  EXPECT_CALL(*mock_ack_listener, OnPacketRetransmitted(9));
  stream_->OnStreamFrameRetransmitted(9, 9, false);

  // kData2 is acked.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(9, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(9, 9, false, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  // Stream is waiting for acks as FIN is not acked.
  EXPECT_TRUE(stream_->IsWaitingForAcks());
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());

  // FIN is acked.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(0, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(18, 0, true, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  EXPECT_FALSE(stream_->IsWaitingForAcks());
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());
}

TEST_P(QuicSpdyStreamTest, StreamDataGetAckedMultipleTimes) {
  Initialize(kShouldProcessData);
  QuicReferenceCountedPointer<MockAckListener> mock_ack_listener(
      new StrictMock<MockAckListener>);
  stream_->set_ack_listener(mock_ack_listener);
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  // Send [0, 27) and fin.
  stream_->WriteOrBufferData("FooAndBar", false, nullptr);
  stream_->WriteOrBufferData("FooAndBar", false, nullptr);
  stream_->WriteOrBufferData("FooAndBar", true, nullptr);

  // Ack [0, 9), [5, 22) and [18, 26)
  // Verify [0, 9) 9 bytes are acked.
  QuicByteCount newly_acked_length = 0;
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(9, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(0, 9, false, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  EXPECT_EQ(2u, QuicStreamPeer::SendBuffer(stream_).size());
  // Verify [9, 22) 13 bytes are acked.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(13, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(5, 17, false, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  EXPECT_EQ(1u, QuicStreamPeer::SendBuffer(stream_).size());
  // Verify [22, 26) 4 bytes are acked.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(4, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(18, 8, false, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  EXPECT_EQ(1u, QuicStreamPeer::SendBuffer(stream_).size());
  EXPECT_TRUE(stream_->IsWaitingForAcks());

  // Ack [0, 27).
  // Verify [26, 27) 1 byte is acked.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(1, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(26, 1, false, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());
  EXPECT_TRUE(stream_->IsWaitingForAcks());

  // Ack Fin. Verify OnPacketAcked is called.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(0, _));
  EXPECT_TRUE(stream_->OnStreamFrameAcked(27, 0, true, QuicTime::Delta::Zero(),
                                          QuicTime::Zero(),
                                          &newly_acked_length));
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());
  EXPECT_FALSE(stream_->IsWaitingForAcks());

  // Ack [10, 27) and fin.
  // No new data is acked, verify OnPacketAcked is not called.
  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(_, _)).Times(0);
  EXPECT_FALSE(
      stream_->OnStreamFrameAcked(10, 17, true, QuicTime::Delta::Zero(),
                                  QuicTime::Zero(), &newly_acked_length));
  EXPECT_EQ(0u, QuicStreamPeer::SendBuffer(stream_).size());
  EXPECT_FALSE(stream_->IsWaitingForAcks());
}

// HTTP/3 only.
TEST_P(QuicSpdyStreamTest, HeadersAckNotReportedWriteOrBufferBody) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  QuicReferenceCountedPointer<MockAckListener> mock_ack_listener(
      new StrictMock<MockAckListener>);
  stream_->set_ack_listener(mock_ack_listener);
  std::string body = "Test1";
  std::string body2(100, 'x');

  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  stream_->WriteOrBufferBody(body, false);
  stream_->WriteOrBufferBody(body2, true);

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(body.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);

  header_length =
      HttpEncoder::SerializeDataFrameHeader(body2.length(), &buffer);
  std::string header2 = std::string(buffer.get(), header_length);

  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(body.length(), _));
  QuicStreamFrame frame(stream_->id(), false, 0, header + body);
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));

  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(0, _));
  QuicStreamFrame frame2(stream_->id(), false, header.length() + body.length(),
                         header2);
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame2), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));

  EXPECT_CALL(*mock_ack_listener, OnPacketAcked(body2.length(), _));
  QuicStreamFrame frame3(stream_->id(), true,
                         header.length() + body.length() + header2.length(),
                         body2);
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame3), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));

  EXPECT_TRUE(
      QuicSpdyStreamPeer::unacked_frame_headers_offsets(stream_).Empty());
}

// HTTP/3 only.
TEST_P(QuicSpdyStreamTest, HeadersAckNotReportedWriteBodySlices) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  QuicReferenceCountedPointer<MockAckListener> mock_ack_listener(
      new StrictMock<MockAckListener>);
  stream_->set_ack_listener(mock_ack_listener);
  std::string body1 = "Test1";
  std::string body2(100, 'x');
  struct iovec body1_iov = {const_cast<char*>(body1.data()), body1.length()};
  struct iovec body2_iov = {const_cast<char*>(body2.data()), body2.length()};
  QuicMemSliceStorage storage(&body1_iov, 1,
                              helper_.GetStreamSendBufferAllocator(), 1024);
  QuicMemSliceStorage storage2(&body2_iov, 1,
                               helper_.GetStreamSendBufferAllocator(), 1024);
  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  stream_->WriteBodySlices(storage.ToSpan(), false);
  stream_->WriteBodySlices(storage2.ToSpan(), true);

  std::string data1 = DataFrame(body1);
  std::string data2 = DataFrame(body2);

  EXPECT_CALL(*mock_ack_listener,
              OnPacketAcked(body1.length() + body2.length(), _));
  QuicStreamFrame frame(stream_->id(), true, 0, data1 + data2);
  EXPECT_TRUE(session_->OnFrameAcked(QuicFrame(frame), QuicTime::Delta::Zero(),
                                     QuicTime::Zero()));

  EXPECT_TRUE(
      QuicSpdyStreamPeer::unacked_frame_headers_offsets(stream_).Empty());
}

// HTTP/3 only.
TEST_P(QuicSpdyStreamTest, HeaderBytesNotReportedOnRetransmission) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  QuicReferenceCountedPointer<MockAckListener> mock_ack_listener(
      new StrictMock<MockAckListener>);
  stream_->set_ack_listener(mock_ack_listener);
  std::string body1 = "Test1";
  std::string body2(100, 'x');

  EXPECT_CALL(*session_, WritevData(_, _, _, _, _, _)).Times(AtLeast(1));
  stream_->WriteOrBufferBody(body1, false);
  stream_->WriteOrBufferBody(body2, true);

  std::string data1 = DataFrame(body1);
  std::string data2 = DataFrame(body2);

  EXPECT_CALL(*mock_ack_listener, OnPacketRetransmitted(body1.length()));
  QuicStreamFrame frame(stream_->id(), false, 0, data1);
  session_->OnStreamFrameRetransmitted(frame);

  EXPECT_CALL(*mock_ack_listener, OnPacketRetransmitted(body2.length()));
  QuicStreamFrame frame2(stream_->id(), true, data1.length(), data2);
  session_->OnStreamFrameRetransmitted(frame2);

  EXPECT_FALSE(
      QuicSpdyStreamPeer::unacked_frame_headers_offsets(stream_).Empty());
}

TEST_P(QuicSpdyStreamTest, HeadersFrameOnRequestStream) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});
  std::string data = DataFrame(kDataFramePayload);
  std::string trailers =
      HeadersFrame({std::make_pair("custom-key", "custom-value")});

  std::string stream_frame_payload =
      quiche::QuicheStrCat(headers, data, trailers);
  QuicStreamFrame frame(stream_->id(), false, 0, stream_frame_payload);
  stream_->OnStreamFrame(frame);

  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));

  // QuicSpdyStream only calls OnBodyAvailable()
  // after the header list has been consumed.
  EXPECT_EQ("", stream_->data());
  stream_->ConsumeHeaderList();
  EXPECT_EQ(kDataFramePayload, stream_->data());

  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("custom-key", "custom-value")));
}

TEST_P(QuicSpdyStreamTest, ProcessBodyAfterTrailers) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(!kShouldProcessData);

  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});
  std::string data = DataFrame(kDataFramePayload);

  // A header block that will take more than one block of sequencer buffer.
  // This ensures that when the trailers are consumed, some buffer buckets will
  // be freed.
  SpdyHeaderBlock trailers_block;
  trailers_block["key1"] = std::string(10000, 'x');
  std::string trailers = HeadersFrame(trailers_block);

  // Feed all three HTTP/3 frames in a single stream frame.
  std::string stream_frame_payload =
      quiche::QuicheStrCat(headers, data, trailers);
  QuicStreamFrame frame(stream_->id(), false, 0, stream_frame_payload);
  stream_->OnStreamFrame(frame);

  stream_->ConsumeHeaderList();
  stream_->MarkTrailersConsumed();

  EXPECT_TRUE(stream_->trailers_decompressed());
  EXPECT_EQ(trailers_block, stream_->received_trailers());

  EXPECT_TRUE(stream_->HasBytesToRead());

  // Consume data.
  char buffer[2048];
  struct iovec vec;
  vec.iov_base = buffer;
  vec.iov_len = QUICHE_ARRAYSIZE(buffer);
  size_t bytes_read = stream_->Readv(&vec, 1);
  EXPECT_EQ(kDataFramePayload, quiche::QuicheStringPiece(buffer, bytes_read));

  EXPECT_FALSE(stream_->HasBytesToRead());
}

// The test stream will receive a stream frame containing malformed headers and
// normal body. Make sure the http decoder stops processing body after the
// connection shuts down.
TEST_P(QuicSpdyStreamTest, MalformedHeadersStopHttpDecoder) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  testing::InSequence s;
  connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));

  // Random bad headers.
  std::string headers =
      HeadersFrame(quiche::QuicheTextUtils::HexDecode("00002a94e7036261"));
  std::string data = DataFrame(kDataFramePayload);

  std::string stream_frame_payload = quiche::QuicheStrCat(headers, data);
  QuicStreamFrame frame(stream_->id(), false, 0, stream_frame_payload);

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_DECOMPRESSION_FAILED,
                      MatchesRegex("Error decoding headers on stream \\d+: "
                                   "Incomplete header block."),
                      _))
      .WillOnce(
          (Invoke([this](QuicErrorCode error, const std::string& error_details,
                         ConnectionCloseBehavior connection_close_behavior) {
            connection_->ReallyCloseConnection(error, error_details,
                                               connection_close_behavior);
          })));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(*session_, OnConnectionClosed(_, _))
      .WillOnce(Invoke([this](const QuicConnectionCloseFrame& frame,
                              ConnectionCloseSource source) {
        session_->ReallyOnConnectionClosed(frame, source);
      }));
  EXPECT_CALL(*session_, SendRstStream(_, _, _));
  EXPECT_CALL(*session_, SendRstStream(_, _, _));
  stream_->OnStreamFrame(frame);
}

// Regression test for https://crbug.com/1027895: a HEADERS frame triggers an
// error in QuicSpdyStream::OnHeadersFramePayload().  This closes the
// connection, freeing the buffer of QuicStreamSequencer.  Therefore
// QuicStreamSequencer::MarkConsumed() must not be called from
// QuicSpdyStream::OnHeadersFramePayload().
TEST_P(QuicSpdyStreamTest, DoNotMarkConsumedAfterQpackDecodingError) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  connection_->AdvanceTime(QuicTime::Delta::FromSeconds(1));

  testing::InSequence s;
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_DECOMPRESSION_FAILED,
                      MatchesRegex("Error decoding headers on stream \\d+: "
                                   "Invalid relative index."),
                      _))
      .WillOnce(
          (Invoke([this](QuicErrorCode error, const std::string& error_details,
                         ConnectionCloseBehavior connection_close_behavior) {
            connection_->ReallyCloseConnection(error, error_details,
                                               connection_close_behavior);
          })));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(*session_, OnConnectionClosed(_, _))
      .WillOnce(Invoke([this](const QuicConnectionCloseFrame& frame,
                              ConnectionCloseSource source) {
        session_->ReallyOnConnectionClosed(frame, source);
      }));
  EXPECT_CALL(*session_, SendRstStream(stream_->id(), _, _));
  EXPECT_CALL(*session_, SendRstStream(stream2_->id(), _, _));

  // Invalid headers: Required Insert Count is zero, but the header block
  // contains a dynamic table reference.
  std::string headers =
      HeadersFrame(quiche::QuicheTextUtils::HexDecode("000080"));
  QuicStreamFrame frame(stream_->id(), false, 0, headers);
  stream_->OnStreamFrame(frame);
}

TEST_P(QuicSpdyStreamTest, ImmediateHeaderDecodingWithDynamicTableEntries) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  testing::InSequence s;
  session_->qpack_decoder()->OnSetDynamicTableCapacity(1024);
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  auto decoder_send_stream =
      QuicSpdySessionPeer::GetQpackDecoderSendStream(session_.get());

  // Deliver dynamic table entry to decoder.
  session_->qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");

  // HEADERS frame referencing first dynamic table entry.
  std::string encoded_headers = quiche::QuicheTextUtils::HexDecode("020080");
  std::string headers = HeadersFrame(encoded_headers);
  EXPECT_CALL(debug_visitor,
              OnHeadersFrameReceived(stream_->id(), encoded_headers.length()));
  // Decoder stream type and header acknowledgement.
  EXPECT_CALL(*session_, WritevData(decoder_send_stream->id(), _, _, _, _, _));
  EXPECT_CALL(debug_visitor, OnHeadersDecoded(stream_->id(), _));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, 0, headers));

  // Headers can be decoded immediately.
  EXPECT_TRUE(stream_->headers_decompressed());

  // Verify headers.
  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // DATA frame.
  std::string data = DataFrame(kDataFramePayload);
  EXPECT_CALL(debug_visitor,
              OnDataFrameReceived(stream_->id(), strlen(kDataFramePayload)));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, /* offset = */
                                         headers.length(), data));
  EXPECT_EQ(kDataFramePayload, stream_->data());

  // Deliver second dynamic table entry to decoder.
  session_->qpack_decoder()->OnInsertWithoutNameReference("trailing", "foobar");

  // Trailing HEADERS frame referencing second dynamic table entry.
  std::string encoded_trailers = quiche::QuicheTextUtils::HexDecode("030080");
  std::string trailers = HeadersFrame(encoded_trailers);
  EXPECT_CALL(debug_visitor,
              OnHeadersFrameReceived(stream_->id(), encoded_trailers.length()));
  // Header acknowledgement.
  EXPECT_CALL(*session_, WritevData(decoder_send_stream->id(), _, _, _, _, _));
  EXPECT_CALL(debug_visitor, OnHeadersDecoded(stream_->id(), _));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), true, /* offset = */
                                         headers.length() + data.length(),
                                         trailers));

  // Trailers can be decoded immediately.
  EXPECT_TRUE(stream_->trailers_decompressed());

  // Verify trailers.
  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("trailing", "foobar")));
  stream_->MarkTrailersConsumed();
}

TEST_P(QuicSpdyStreamTest, BlockedHeaderDecoding) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  testing::InSequence s;
  session_->qpack_decoder()->OnSetDynamicTableCapacity(1024);
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  // HEADERS frame referencing first dynamic table entry.
  std::string encoded_headers = quiche::QuicheTextUtils::HexDecode("020080");
  std::string headers = HeadersFrame(encoded_headers);
  EXPECT_CALL(debug_visitor,
              OnHeadersFrameReceived(stream_->id(), encoded_headers.length()));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, 0, headers));

  // Decoding is blocked because dynamic table entry has not been received yet.
  EXPECT_FALSE(stream_->headers_decompressed());

  auto decoder_send_stream =
      QuicSpdySessionPeer::GetQpackDecoderSendStream(session_.get());

  // Decoder stream type and header acknowledgement.
  EXPECT_CALL(*session_, WritevData(decoder_send_stream->id(), _, _, _, _, _));
  EXPECT_CALL(debug_visitor, OnHeadersDecoded(stream_->id(), _));
  // Deliver dynamic table entry to decoder.
  session_->qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");
  EXPECT_TRUE(stream_->headers_decompressed());

  // Verify headers.
  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // DATA frame.
  std::string data = DataFrame(kDataFramePayload);
  EXPECT_CALL(debug_visitor,
              OnDataFrameReceived(stream_->id(), strlen(kDataFramePayload)));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, /* offset = */
                                         headers.length(), data));
  EXPECT_EQ(kDataFramePayload, stream_->data());

  // Trailing HEADERS frame referencing second dynamic table entry.
  std::string encoded_trailers = quiche::QuicheTextUtils::HexDecode("030080");
  std::string trailers = HeadersFrame(encoded_trailers);
  EXPECT_CALL(debug_visitor,
              OnHeadersFrameReceived(stream_->id(), encoded_trailers.length()));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), true, /* offset = */
                                         headers.length() + data.length(),
                                         trailers));

  // Decoding is blocked because dynamic table entry has not been received yet.
  EXPECT_FALSE(stream_->trailers_decompressed());

  EXPECT_CALL(*session_, WritevData(decoder_send_stream->id(), _, _, _, _, _));
  EXPECT_CALL(debug_visitor, OnHeadersDecoded(stream_->id(), _));
  // Deliver second dynamic table entry to decoder.
  session_->qpack_decoder()->OnInsertWithoutNameReference("trailing", "foobar");
  EXPECT_TRUE(stream_->trailers_decompressed());

  // Verify trailers.
  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("trailing", "foobar")));
  stream_->MarkTrailersConsumed();
}

TEST_P(QuicSpdyStreamTest, AsyncErrorDecodingHeaders) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  session_->qpack_decoder()->OnSetDynamicTableCapacity(1024);

  // HEADERS frame only referencing entry with absolute index 0 but with
  // Required Insert Count = 2, which is incorrect.
  std::string headers =
      HeadersFrame(quiche::QuicheTextUtils::HexDecode("030081"));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, 0, headers));

  // Even though entire header block is received and every referenced entry is
  // available, decoding is blocked until insert count reaches the Required
  // Insert Count value advertised in the header block prefix.
  EXPECT_FALSE(stream_->headers_decompressed());

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_DECOMPRESSION_FAILED,
                      MatchesRegex("Error decoding headers on stream \\d+: "
                                   "Required Insert Count too large."),
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));

  // Deliver two dynamic table entries to decoder
  // to trigger decoding of header block.
  session_->qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");
  session_->qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");
}

// Regression test for https://crbug.com/1024263 and for
// https://crbug.com/1025209#c11.
TEST_P(QuicSpdyStreamTest, BlockedHeaderDecodingUnblockedWithBufferedError) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  session_->qpack_decoder()->OnSetDynamicTableCapacity(1024);

  // Relative index 2 is invalid because it is larger than or equal to the Base.
  std::string headers =
      HeadersFrame(quiche::QuicheTextUtils::HexDecode("020082"));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, 0, headers));

  // Decoding is blocked.
  EXPECT_FALSE(stream_->headers_decompressed());

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_DECOMPRESSION_FAILED,
                      MatchesRegex("Error decoding headers on stream \\d+: "
                                   "Invalid relative index."),
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));

  // Deliver one dynamic table entry to decoder
  // to trigger decoding of header block.
  session_->qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");
}

TEST_P(QuicSpdyStreamTest, AsyncErrorDecodingTrailers) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  testing::InSequence s;
  session_->qpack_decoder()->OnSetDynamicTableCapacity(1024);

  // HEADERS frame referencing first dynamic table entry.
  std::string headers =
      HeadersFrame(quiche::QuicheTextUtils::HexDecode("020080"));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, 0, headers));

  // Decoding is blocked because dynamic table entry has not been received yet.
  EXPECT_FALSE(stream_->headers_decompressed());

  auto decoder_send_stream =
      QuicSpdySessionPeer::GetQpackDecoderSendStream(session_.get());

  // The stream byte will be written in the first byte.
  EXPECT_CALL(*session_, WritevData(decoder_send_stream->id(), _, _, _, _, _));
  // Deliver dynamic table entry to decoder.
  session_->qpack_decoder()->OnInsertWithoutNameReference("foo", "bar");
  EXPECT_TRUE(stream_->headers_decompressed());

  // Verify headers.
  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // DATA frame.
  std::string data = DataFrame(kDataFramePayload);
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, /* offset = */
                                         headers.length(), data));
  EXPECT_EQ(kDataFramePayload, stream_->data());

  // Trailing HEADERS frame only referencing entry with absolute index 0 but
  // with Required Insert Count = 2, which is incorrect.
  std::string trailers =
      HeadersFrame(quiche::QuicheTextUtils::HexDecode("030081"));
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), true, /* offset = */
                                         headers.length() + data.length(),
                                         trailers));

  // Even though entire header block is received and every referenced entry is
  // available, decoding is blocked until insert count reaches the Required
  // Insert Count value advertised in the header block prefix.
  EXPECT_FALSE(stream_->trailers_decompressed());

  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_QPACK_DECOMPRESSION_FAILED,
                      MatchesRegex("Error decoding trailers on stream \\d+: "
                                   "Required Insert Count too large."),
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET));

  // Deliver second dynamic table entry to decoder
  // to trigger decoding of trailing header block.
  session_->qpack_decoder()->OnInsertWithoutNameReference("trailing", "foobar");
}

class QuicSpdyStreamIncrementalConsumptionTest : public QuicSpdyStreamTest {
 protected:
  QuicSpdyStreamIncrementalConsumptionTest() : offset_(0), consumed_bytes_(0) {}
  ~QuicSpdyStreamIncrementalConsumptionTest() override = default;

  // Create QuicStreamFrame with |payload|
  // and pass it to stream_->OnStreamFrame().
  void OnStreamFrame(quiche::QuicheStringPiece payload) {
    QuicStreamFrame frame(stream_->id(), /* fin = */ false, offset_, payload);
    stream_->OnStreamFrame(frame);
    offset_ += payload.size();
  }

  // Return number of bytes marked consumed with sequencer
  // since last NewlyConsumedBytes() call.
  QuicStreamOffset NewlyConsumedBytes() {
    QuicStreamOffset previously_consumed_bytes = consumed_bytes_;
    consumed_bytes_ = stream_->sequencer()->NumBytesConsumed();
    return consumed_bytes_ - previously_consumed_bytes;
  }

  // Read |size| bytes from the stream.
  std::string ReadFromStream(QuicByteCount size) {
    std::string buffer;
    buffer.resize(size);

    struct iovec vec;
    vec.iov_base = const_cast<char*>(buffer.data());
    vec.iov_len = size;

    size_t bytes_read = stream_->Readv(&vec, 1);
    EXPECT_EQ(bytes_read, size);

    return buffer;
  }

 private:
  QuicStreamOffset offset_;
  QuicStreamOffset consumed_bytes_;
};

INSTANTIATE_TEST_SUITE_P(Tests,
                         QuicSpdyStreamIncrementalConsumptionTest,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

// Test that stream bytes are consumed (by calling
// sequencer()->MarkConsumed()) incrementally, as soon as possible.
TEST_P(QuicSpdyStreamIncrementalConsumptionTest, OnlyKnownFrames) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(!kShouldProcessData);

  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});

  // All HEADERS frame bytes are consumed even if the frame is not received
  // completely.
  OnStreamFrame(
      quiche::QuicheStringPiece(headers).substr(0, headers.size() - 1));
  EXPECT_EQ(headers.size() - 1, NewlyConsumedBytes());

  // The rest of the HEADERS frame is also consumed immediately.
  OnStreamFrame(quiche::QuicheStringPiece(headers).substr(headers.size() - 1));
  EXPECT_EQ(1u, NewlyConsumedBytes());

  // Verify headers.
  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // DATA frame.
  quiche::QuicheStringPiece data_payload(kDataFramePayload);
  std::string data_frame = DataFrame(data_payload);
  QuicByteCount data_frame_header_length =
      data_frame.size() - data_payload.size();

  // DATA frame header is consumed.
  // DATA frame payload is not consumed because payload has to be buffered.
  OnStreamFrame(data_frame);
  EXPECT_EQ(data_frame_header_length, NewlyConsumedBytes());

  // Consume all but last byte of data.
  EXPECT_EQ(data_payload.substr(0, data_payload.size() - 1),
            ReadFromStream(data_payload.size() - 1));
  EXPECT_EQ(data_payload.size() - 1, NewlyConsumedBytes());

  std::string trailers =
      HeadersFrame({std::make_pair("custom-key", "custom-value")});

  // No bytes are consumed, because last byte of DATA payload is still buffered.
  OnStreamFrame(
      quiche::QuicheStringPiece(trailers).substr(0, trailers.size() - 1));
  EXPECT_EQ(0u, NewlyConsumedBytes());

  // Reading last byte of DATA payload triggers consumption of all data received
  // so far, even though last HEADERS frame has not been received completely.
  EXPECT_EQ(data_payload.substr(data_payload.size() - 1), ReadFromStream(1));
  EXPECT_EQ(1 + trailers.size() - 1, NewlyConsumedBytes());

  // Last byte of trailers is immediately consumed.
  OnStreamFrame(
      quiche::QuicheStringPiece(trailers).substr(trailers.size() - 1));
  EXPECT_EQ(1u, NewlyConsumedBytes());

  // Verify trailers.
  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("custom-key", "custom-value")));
}

TEST_P(QuicSpdyStreamIncrementalConsumptionTest, ReceiveUnknownFrame) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);
  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  EXPECT_CALL(debug_visitor,
              OnUnknownFrameReceived(stream_->id(), /* frame_type = */ 0x21,
                                     /* payload_length = */ 3));
  std::string unknown_frame = UnknownFrame(0x21, "foo");
  OnStreamFrame(unknown_frame);
}

TEST_P(QuicSpdyStreamIncrementalConsumptionTest, UnknownFramesInterleaved) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(!kShouldProcessData);

  // Unknown frame of reserved type before HEADERS is consumed immediately.
  std::string unknown_frame1 = UnknownFrame(0x21, "foo");
  OnStreamFrame(unknown_frame1);
  EXPECT_EQ(unknown_frame1.size(), NewlyConsumedBytes());

  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});

  // All HEADERS frame bytes are consumed even if the frame is not received
  // completely.
  OnStreamFrame(
      quiche::QuicheStringPiece(headers).substr(0, headers.size() - 1));
  EXPECT_EQ(headers.size() - 1, NewlyConsumedBytes());

  // The rest of the HEADERS frame is also consumed immediately.
  OnStreamFrame(quiche::QuicheStringPiece(headers).substr(headers.size() - 1));
  EXPECT_EQ(1u, NewlyConsumedBytes());

  // Verify headers.
  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // Frame of unknown, not reserved type between HEADERS and DATA is consumed
  // immediately.
  std::string unknown_frame2 = UnknownFrame(0x3a, "");
  OnStreamFrame(unknown_frame2);
  EXPECT_EQ(unknown_frame2.size(), NewlyConsumedBytes());

  // DATA frame.
  quiche::QuicheStringPiece data_payload(kDataFramePayload);
  std::string data_frame = DataFrame(data_payload);
  QuicByteCount data_frame_header_length =
      data_frame.size() - data_payload.size();

  // DATA frame header is consumed.
  // DATA frame payload is not consumed because payload has to be buffered.
  OnStreamFrame(data_frame);
  EXPECT_EQ(data_frame_header_length, NewlyConsumedBytes());

  // Frame of unknown, not reserved type is not consumed because DATA payload is
  // still buffered.
  std::string unknown_frame3 = UnknownFrame(0x39, "bar");
  OnStreamFrame(unknown_frame3);
  EXPECT_EQ(0u, NewlyConsumedBytes());

  // Consume all but last byte of data.
  EXPECT_EQ(data_payload.substr(0, data_payload.size() - 1),
            ReadFromStream(data_payload.size() - 1));
  EXPECT_EQ(data_payload.size() - 1, NewlyConsumedBytes());

  std::string trailers =
      HeadersFrame({std::make_pair("custom-key", "custom-value")});

  // No bytes are consumed, because last byte of DATA payload is still buffered.
  OnStreamFrame(
      quiche::QuicheStringPiece(trailers).substr(0, trailers.size() - 1));
  EXPECT_EQ(0u, NewlyConsumedBytes());

  // Reading last byte of DATA payload triggers consumption of all data received
  // so far, even though last HEADERS frame has not been received completely.
  EXPECT_EQ(data_payload.substr(data_payload.size() - 1), ReadFromStream(1));
  EXPECT_EQ(1 + unknown_frame3.size() + trailers.size() - 1,
            NewlyConsumedBytes());

  // Last byte of trailers is immediately consumed.
  OnStreamFrame(
      quiche::QuicheStringPiece(trailers).substr(trailers.size() - 1));
  EXPECT_EQ(1u, NewlyConsumedBytes());

  // Verify trailers.
  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("custom-key", "custom-value")));

  // Unknown frame of reserved type after trailers is consumed immediately.
  std::string unknown_frame4 = UnknownFrame(0x40, "");
  OnStreamFrame(unknown_frame4);
  EXPECT_EQ(unknown_frame4.size(), NewlyConsumedBytes());
}

TEST_P(QuicSpdyStreamTest, PushPromiseOnDataStream) {
  Initialize(kShouldProcessData);
  if (!UsesHttp3()) {
    return;
  }

  StrictMock<MockHttp3DebugVisitor> debug_visitor;
  session_->set_debug_visitor(&debug_visitor);

  SpdyHeaderBlock pushed_headers;
  pushed_headers["foo"] = "bar";
  std::string headers = EncodeQpackHeaders(pushed_headers);

  const QuicStreamId push_id = 1;
  PushPromiseFrame push_promise;
  push_promise.push_id = push_id;
  push_promise.headers = headers;
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializePushPromiseFrameWithOnlyPushId(
      push_promise, &buffer);
  std::string data = std::string(buffer.get(), length) + headers;
  QuicStreamFrame frame(stream_->id(), false, 0, data);

  EXPECT_CALL(debug_visitor, OnPushPromiseFrameReceived(stream_->id(), push_id,
                                                        headers.length()));
  EXPECT_CALL(debug_visitor,
              OnPushPromiseDecoded(stream_->id(), push_id,
                                   AsHeaderList(pushed_headers)));
  EXPECT_CALL(*session_,
              OnPromiseHeaderList(stream_->id(), push_promise.push_id,
                                  headers.length(), _));
  stream_->OnStreamFrame(frame);
}

// Close connection if a DATA frame is received before a HEADERS frame.
TEST_P(QuicSpdyStreamTest, DataBeforeHeaders) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Closing the connection is mocked out in tests.  Instead, simply stop
  // reading data at the stream level to prevent QuicSpdyStream from blowing up.
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_SPDY_STREAM,
                      "Unexpected DATA frame received.",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET))
      .WillOnce(InvokeWithoutArgs([this]() { stream_->StopReading(); }));

  std::string data = DataFrame(kDataFramePayload);
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, 0, data));
}

// Close connection if a HEADERS frame is received after the trailing HEADERS.
TEST_P(QuicSpdyStreamTest, TrailersAfterTrailers) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Receive and consume headers.
  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});
  QuicStreamOffset offset = 0;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), false, offset, headers));
  offset += headers.size();

  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // Receive data.  It is consumed by TestStream.
  std::string data = DataFrame(kDataFramePayload);
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, offset, data));
  offset += data.size();

  EXPECT_EQ(kDataFramePayload, stream_->data());

  // Receive and consume trailers.
  std::string trailers1 =
      HeadersFrame({std::make_pair("custom-key", "custom-value")});
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), false, offset, trailers1));
  offset += trailers1.size();

  EXPECT_TRUE(stream_->trailers_decompressed());
  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("custom-key", "custom-value")));

  // Closing the connection is mocked out in tests.  Instead, simply stop
  // reading data at the stream level to prevent QuicSpdyStream from blowing up.
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_SPDY_STREAM,
                      "HEADERS frame received after trailing HEADERS.",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET))
      .WillOnce(InvokeWithoutArgs([this]() { stream_->StopReading(); }));

  // Receive another HEADERS frame, with no header fields.
  std::string trailers2 = HeadersFrame(SpdyHeaderBlock());
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), false, offset, trailers2));
}

// Regression test for https://crbug.com/978733.
// Close connection if a DATA frame is received after the trailing HEADERS.
TEST_P(QuicSpdyStreamTest, DataAfterTrailers) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // Receive and consume headers.
  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});
  QuicStreamOffset offset = 0;
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), false, offset, headers));
  offset += headers.size();

  EXPECT_THAT(stream_->header_list(), ElementsAre(Pair("foo", "bar")));
  stream_->ConsumeHeaderList();

  // Receive data.  It is consumed by TestStream.
  std::string data1 = DataFrame(kDataFramePayload);
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, offset, data1));
  offset += data1.size();
  EXPECT_EQ(kDataFramePayload, stream_->data());

  // Receive trailers, with single header field "custom-key: custom-value".
  std::string trailers =
      HeadersFrame({std::make_pair("custom-key", "custom-value")});
  stream_->OnStreamFrame(
      QuicStreamFrame(stream_->id(), false, offset, trailers));
  offset += trailers.size();

  EXPECT_THAT(stream_->received_trailers(),
              ElementsAre(Pair("custom-key", "custom-value")));

  // Closing the connection is mocked out in tests.  Instead, simply stop
  // reading data at the stream level to prevent QuicSpdyStream from blowing up.
  EXPECT_CALL(
      *connection_,
      CloseConnection(QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_SPDY_STREAM,
                      "Unexpected DATA frame received.",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET))
      .WillOnce(InvokeWithoutArgs([this]() { stream_->StopReading(); }));

  // Receive more data.
  std::string data2 = DataFrame("This payload should not be proccessed.");
  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), false, offset, data2));
}

// SETTINGS frames are invalid on bidirectional streams.  If one is received,
// the connection is closed.  No more data should be processed.
TEST_P(QuicSpdyStreamTest, StopProcessingIfConnectionClosed) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  // SETTINGS frame with empty payload.
  std::string settings = quiche::QuicheTextUtils::HexDecode("0400");

  // HEADERS frame.
  // Since it arrives after a SETTINGS frame, it should never be read.
  std::string headers = HeadersFrame({std::make_pair("foo", "bar")});

  // Combine the two frames to make sure they are processed in a single
  // QuicSpdyStream::OnDataAvailable() call.
  std::string frames = quiche::QuicheStrCat(settings, headers);

  EXPECT_EQ(0u, stream_->sequencer()->NumBytesConsumed());

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_HTTP_FRAME_UNEXPECTED_ON_SPDY_STREAM, _, _))
      .WillOnce(
          Invoke(connection_, &MockQuicConnection::ReallyCloseConnection));
  EXPECT_CALL(*connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(*session_, OnConnectionClosed(_, _));

  stream_->OnStreamFrame(QuicStreamFrame(stream_->id(), /* fin = */ false,
                                         /* offset = */ 0, frames));

  EXPECT_EQ(0u, stream_->sequencer()->NumBytesConsumed());
}

// Stream Cancellation instruction is sent on QPACK decoder stream
// when stream is reset.
TEST_P(QuicSpdyStreamTest, StreamCancellationWhenStreamReset) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  auto qpack_decoder_stream =
      QuicSpdySessionPeer::GetQpackDecoderSendStream(session_.get());
  EXPECT_CALL(*session_, WritevData(qpack_decoder_stream->id(), 1, 1, _, _, _));
  EXPECT_CALL(*session_,
              SendRstStream(stream_->id(), QUIC_STREAM_CANCELLED, 0));

  stream_->Reset(QUIC_STREAM_CANCELLED);
}

// Stream Cancellation instruction is sent on QPACK decoder stream
// when RESET_STREAM frame is received.
TEST_P(QuicSpdyStreamTest, StreamCancellationOnResetReceived) {
  if (!UsesHttp3()) {
    return;
  }

  Initialize(kShouldProcessData);

  auto qpack_decoder_stream =
      QuicSpdySessionPeer::GetQpackDecoderSendStream(session_.get());
  EXPECT_CALL(*session_, WritevData(qpack_decoder_stream->id(), 1, 1, _, _, _));

  stream_->OnStreamReset(QuicRstStreamFrame(
      kInvalidControlFrameId, stream_->id(), QUIC_STREAM_CANCELLED, 0));
}

}  // namespace
}  // namespace test
}  // namespace quic
