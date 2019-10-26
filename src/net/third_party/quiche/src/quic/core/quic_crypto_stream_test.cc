// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;

namespace quic {
namespace test {
namespace {

class MockQuicCryptoStream : public QuicCryptoStream,
                             public QuicCryptoHandshaker {
 public:
  explicit MockQuicCryptoStream(QuicSession* session)
      : QuicCryptoStream(session),
        QuicCryptoHandshaker(this, session),
        params_(new QuicCryptoNegotiatedParameters) {}
  MockQuicCryptoStream(const MockQuicCryptoStream&) = delete;
  MockQuicCryptoStream& operator=(const MockQuicCryptoStream&) = delete;

  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override {
    messages_.push_back(message);
  }

  std::vector<CryptoHandshakeMessage>* messages() { return &messages_; }

  bool encryption_established() const override { return false; }
  bool handshake_confirmed() const override { return false; }

  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override {
    return *params_;
  }
  CryptoMessageParser* crypto_message_parser() override {
    return QuicCryptoHandshaker::crypto_message_parser();
  }

 private:
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
  std::vector<CryptoHandshakeMessage> messages_;
};

class QuicCryptoStreamTest : public QuicTest {
 public:
  QuicCryptoStreamTest()
      : connection_(new MockQuicConnection(&helper_,
                                           &alarm_factory_,
                                           Perspective::IS_CLIENT)),
        session_(connection_, /*create_mock_crypto_stream=*/false) {
    stream_ = new MockQuicCryptoStream(&session_);
    session_.SetCryptoStream(stream_);
    session_.Initialize();
    message_.set_tag(kSHLO);
    message_.SetStringPiece(1, "abc");
    message_.SetStringPiece(2, "def");
    ConstructHandshakeMessage();
  }
  QuicCryptoStreamTest(const QuicCryptoStreamTest&) = delete;
  QuicCryptoStreamTest& operator=(const QuicCryptoStreamTest&) = delete;

  void ConstructHandshakeMessage() {
    CryptoFramer framer;
    message_data_ = framer.ConstructHandshakeMessage(message_);
  }

 protected:
  MockQuicConnectionHelper helper_;
  MockAlarmFactory alarm_factory_;
  MockQuicConnection* connection_;
  MockQuicSpdySession session_;
  MockQuicCryptoStream* stream_;
  CryptoHandshakeMessage message_;
  std::unique_ptr<QuicData> message_data_;
};

TEST_F(QuicCryptoStreamTest, NotInitiallyConected) {
  EXPECT_FALSE(stream_->encryption_established());
  EXPECT_FALSE(stream_->handshake_confirmed());
}

TEST_F(QuicCryptoStreamTest, ProcessRawData) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    stream_->OnStreamFrame(QuicStreamFrame(
        QuicUtils::GetCryptoStreamId(connection_->transport_version()),
        /*fin=*/false,
        /*offset=*/0, message_data_->AsStringPiece()));
  } else {
    stream_->OnCryptoFrame(QuicCryptoFrame(ENCRYPTION_INITIAL, /*offset*/ 0,
                                           message_data_->AsStringPiece()));
  }
  ASSERT_EQ(1u, stream_->messages()->size());
  const CryptoHandshakeMessage& message = (*stream_->messages())[0];
  EXPECT_EQ(kSHLO, message.tag());
  EXPECT_EQ(2u, message.tag_value_map().size());
  EXPECT_EQ("abc", crypto_test_utils::GetValueForTag(message, 1));
  EXPECT_EQ("def", crypto_test_utils::GetValueForTag(message, 2));
}

TEST_F(QuicCryptoStreamTest, ProcessBadData) {
  std::string bad(message_data_->data(), message_data_->length());
  const int kFirstTagIndex = sizeof(uint32_t) +  // message tag
                             sizeof(uint16_t) +  // number of tag-value pairs
                             sizeof(uint16_t);   // padding
  EXPECT_EQ(1, bad[kFirstTagIndex]);
  bad[kFirstTagIndex] = 0x7F;  // out of order tag

  EXPECT_CALL(*connection_, CloseConnection(QUIC_CRYPTO_TAGS_OUT_OF_ORDER,
                                            testing::_, testing::_));
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    stream_->OnStreamFrame(QuicStreamFrame(
        QuicUtils::GetCryptoStreamId(connection_->transport_version()),
        /*fin=*/false, /*offset=*/0, bad));
  } else {
    stream_->OnCryptoFrame(
        QuicCryptoFrame(ENCRYPTION_INITIAL, /*offset*/ 0, bad));
  }
}

TEST_F(QuicCryptoStreamTest, NoConnectionLevelFlowControl) {
  EXPECT_FALSE(
      QuicStreamPeer::StreamContributesToConnectionFlowControl(stream_));
}

TEST_F(QuicCryptoStreamTest, RetransmitCryptoData) {
  if (QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  InSequence s;
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 0, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->WriteOrBufferData(data, false, nullptr);
  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT.
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 1350, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->WriteOrBufferData(data, false, nullptr);
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  // Lost [0, 1000).
  stream_->OnStreamFrameLost(0, 1000, false);
  EXPECT_TRUE(stream_->HasPendingRetransmission());
  // Lost [1200, 2000).
  stream_->OnStreamFrameLost(1200, 800, false);
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1000, 0, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  // Verify [1200, 2000) are sent in [1200, 1350) and [1350, 2000) because of
  // they are in different encryption levels.
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 150, 1200, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 650, 1350, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->OnCanWrite();
  EXPECT_FALSE(stream_->HasPendingRetransmission());
  // Verify connection's encryption level has restored.
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());
}

TEST_F(QuicCryptoStreamTest, RetransmitCryptoDataInCryptoFrames) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  EXPECT_CALL(*connection_, SendCryptoData(_, _, _)).Times(0);
  InSequence s;
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_INITIAL, data);
  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT.
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  std::unique_ptr<NullEncrypter> encrypter =
      QuicMakeUnique<NullEncrypter>(Perspective::IS_CLIENT);
  connection_->SetEncrypter(ENCRYPTION_ZERO_RTT, std::move(encrypter));
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_ZERO_RTT, data);
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  // Lost [0, 1000).
  QuicCryptoFrame lost_frame(ENCRYPTION_INITIAL, 0, 1000);
  stream_->OnCryptoFrameLost(&lost_frame);
  EXPECT_TRUE(stream_->HasPendingCryptoRetransmission());
  // Lost [1200, 2000).
  lost_frame = QuicCryptoFrame(ENCRYPTION_INITIAL, 1200, 150);
  stream_->OnCryptoFrameLost(&lost_frame);
  lost_frame = QuicCryptoFrame(ENCRYPTION_ZERO_RTT, 0, 650);
  stream_->OnCryptoFrameLost(&lost_frame);
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1000, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  // Verify [1200, 2000) are sent in [1200, 1350) and [1350, 2000) because of
  // they are in different encryption levels.
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 150, 1200))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 650, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WritePendingCryptoRetransmission();
  EXPECT_FALSE(stream_->HasPendingCryptoRetransmission());
  // Verify connection's encryption level has restored.
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());
}

TEST_F(QuicCryptoStreamTest, NeuterUnencryptedStreamData) {
  if (QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 0, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->WriteOrBufferData(data, false, nullptr);
  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT.
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 1350, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->WriteOrBufferData(data, false, nullptr);

  // Lost [0, 1350).
  stream_->OnStreamFrameLost(0, 1350, false);
  EXPECT_TRUE(stream_->HasPendingRetransmission());
  // Neuters [0, 1350).
  stream_->NeuterUnencryptedStreamData();
  EXPECT_FALSE(stream_->HasPendingRetransmission());
  // Lost [0, 1350) again.
  stream_->OnStreamFrameLost(0, 1350, false);
  EXPECT_FALSE(stream_->HasPendingRetransmission());

  // Lost [1350, 2000).
  stream_->OnStreamFrameLost(1350, 650, false);
  EXPECT_TRUE(stream_->HasPendingRetransmission());
  stream_->NeuterUnencryptedStreamData();
  EXPECT_TRUE(stream_->HasPendingRetransmission());
}

TEST_F(QuicCryptoStreamTest, NeuterUnencryptedCryptoData) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_INITIAL, data);
  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT.
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  std::unique_ptr<NullEncrypter> encrypter =
      QuicMakeUnique<NullEncrypter>(Perspective::IS_CLIENT);
  connection_->SetEncrypter(ENCRYPTION_ZERO_RTT, std::move(encrypter));
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());
  EXPECT_CALL(*connection_, SendCryptoData(_, _, _)).Times(0);
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_ZERO_RTT, data);

  // Lost [0, 1350).
  QuicCryptoFrame lost_frame(ENCRYPTION_INITIAL, 0, 1350);
  stream_->OnCryptoFrameLost(&lost_frame);
  EXPECT_TRUE(stream_->HasPendingCryptoRetransmission());
  // Neuters [0, 1350).
  stream_->NeuterUnencryptedStreamData();
  EXPECT_FALSE(stream_->HasPendingCryptoRetransmission());
  // Lost [0, 1350) again.
  stream_->OnCryptoFrameLost(&lost_frame);
  EXPECT_FALSE(stream_->HasPendingCryptoRetransmission());

  // Lost [1350, 2000), which starts at offset 0 at the ENCRYPTION_ZERO_RTT
  // level.
  lost_frame = QuicCryptoFrame(ENCRYPTION_ZERO_RTT, 0, 650);
  stream_->OnCryptoFrameLost(&lost_frame);
  EXPECT_TRUE(stream_->HasPendingCryptoRetransmission());
  stream_->NeuterUnencryptedStreamData();
  EXPECT_TRUE(stream_->HasPendingCryptoRetransmission());
}

TEST_F(QuicCryptoStreamTest, RetransmitStreamData) {
  if (QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  InSequence s;
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 0, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->WriteOrBufferData(data, false, nullptr);
  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT.
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 1350, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->WriteOrBufferData(data, false, nullptr);
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  // Ack [2000, 2500).
  QuicByteCount newly_acked_length = 0;
  stream_->OnStreamFrameAcked(2000, 500, false, QuicTime::Delta::Zero(),
                              &newly_acked_length);
  EXPECT_EQ(500u, newly_acked_length);

  // Force crypto stream to send [1350, 2700) and only [1350, 1500) is consumed.
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 650, 1350, _))
      .WillOnce(InvokeWithoutArgs([this]() {
        return MockQuicSession::ConsumeData(
            stream_,
            QuicUtils::GetCryptoStreamId(connection_->transport_version()), 150,
            1350, NO_FIN);
      }));

  EXPECT_FALSE(stream_->RetransmitStreamData(1350, 1350, false));
  // Verify connection's encryption level has restored.
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  // Force session to send [1350, 1500) again and all data is consumed.
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 650, 1350, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 200, 2500, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  EXPECT_TRUE(stream_->RetransmitStreamData(1350, 1350, false));
  // Verify connection's encryption level has restored.
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  EXPECT_CALL(session_, WritevData(_, _, _, _, _)).Times(0);
  // Force to send an empty frame.
  EXPECT_TRUE(stream_->RetransmitStreamData(0, 0, false));
}

TEST_F(QuicCryptoStreamTest, RetransmitStreamDataWithCryptoFrames) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  InSequence s;
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_INITIAL, data);
  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT.
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  std::unique_ptr<NullEncrypter> encrypter =
      QuicMakeUnique<NullEncrypter>(Perspective::IS_CLIENT);
  connection_->SetEncrypter(ENCRYPTION_ZERO_RTT, std::move(encrypter));
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_ZERO_RTT, data);
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  // Ack [2000, 2500).
  QuicCryptoFrame acked_frame(ENCRYPTION_ZERO_RTT, 650, 500);
  EXPECT_TRUE(
      stream_->OnCryptoFrameAcked(acked_frame, QuicTime::Delta::Zero()));

  // Retransmit only [1350, 1500).
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 150, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  QuicCryptoFrame frame_to_retransmit(ENCRYPTION_ZERO_RTT, 0, 150);
  stream_->RetransmitData(&frame_to_retransmit);

  // Verify connection's encryption level has restored.
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  // Retransmit [1350, 2700) again and all data is sent.
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 650, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 200, 1150))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  frame_to_retransmit = QuicCryptoFrame(ENCRYPTION_ZERO_RTT, 0, 1350);
  stream_->RetransmitData(&frame_to_retransmit);
  // Verify connection's encryption level has restored.
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, connection_->encryption_level());

  EXPECT_CALL(*connection_, SendCryptoData(_, _, _)).Times(0);
  // Force to send an empty frame.
  QuicCryptoFrame empty_frame(ENCRYPTION_FORWARD_SECURE, 0, 0);
  stream_->RetransmitData(&empty_frame);
}

// Regression test for b/115926584.
TEST_F(QuicCryptoStreamTest, HasUnackedCryptoData) {
  if (QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  std::string data(1350, 'a');
  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 0, _))
      .WillOnce(testing::Return(QuicConsumedData(0, false)));
  stream_->WriteOrBufferData(data, false, nullptr);
  EXPECT_FALSE(stream_->IsWaitingForAcks());
  // Although there is no outstanding data, verify session has pending crypto
  // data.
  EXPECT_TRUE(session_.HasUnackedCryptoData());

  EXPECT_CALL(
      session_,
      WritevData(_,
                 QuicUtils::GetCryptoStreamId(connection_->transport_version()),
                 1350, 0, _))
      .WillOnce(Invoke(MockQuicSession::ConsumeData));
  stream_->OnCanWrite();
  EXPECT_TRUE(stream_->IsWaitingForAcks());
  EXPECT_TRUE(session_.HasUnackedCryptoData());
}

TEST_F(QuicCryptoStreamTest, HasUnackedCryptoDataWithCryptoFrames) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1350, 0))
      .WillOnce(Invoke(connection_,
                       &MockQuicConnection::QuicConnection_SendCryptoData));
  stream_->WriteCryptoData(ENCRYPTION_INITIAL, data);
  EXPECT_TRUE(stream_->IsWaitingForAcks());
  EXPECT_TRUE(session_.HasUnackedCryptoData());
}

// Regression test for bugfix of GetPacketHeaderSize.
TEST_F(QuicCryptoStreamTest, CryptoMessageFramingOverhead) {
  SetQuicReloadableFlag(quic_fix_get_packet_header_size, true);
  for (auto version : AllSupportedTransportVersions()) {
    SCOPED_TRACE(version);
    QuicByteCount expected_overhead = 48;
    if (VersionHasIetfInvariantHeader(version)) {
      expected_overhead += 4;
    }
    if (QuicVersionHasLongHeaderLengths(version)) {
      expected_overhead += 3;
    }
    if (VersionHasLengthPrefixedConnectionIds(version)) {
      expected_overhead += 1;
    }
    EXPECT_EQ(expected_overhead, QuicCryptoStream::CryptoMessageFramingOverhead(
                                     version, TestConnectionId()));
  }
}

TEST_F(QuicCryptoStreamTest, WriteBufferedCryptoFrames) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }
  EXPECT_FALSE(stream_->HasBufferedCryptoFrames());
  InSequence s;
  // Send [0, 1350) in ENCRYPTION_INITIAL.
  EXPECT_EQ(ENCRYPTION_INITIAL, connection_->encryption_level());
  std::string data(1350, 'a');
  // Only consumed 1000 bytes.
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 1350, 0))
      .WillOnce(Return(1000));
  stream_->WriteCryptoData(ENCRYPTION_INITIAL, data);
  EXPECT_TRUE(stream_->HasBufferedCryptoFrames());

  // Send [1350, 2700) in ENCRYPTION_ZERO_RTT and verify no write is attempted
  // because there is buffered data.
  EXPECT_CALL(*connection_, SendCryptoData(_, _, _)).Times(0);
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  stream_->WriteCryptoData(ENCRYPTION_ZERO_RTT, data);
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());

  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_INITIAL, 350, 1000))
      .WillOnce(Return(350));
  // Partial write of ENCRYPTION_ZERO_RTT data.
  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 1350, 0))
      .WillOnce(Return(1000));
  stream_->WriteBufferedCryptoFrames();
  EXPECT_TRUE(stream_->HasBufferedCryptoFrames());
  EXPECT_EQ(ENCRYPTION_ZERO_RTT, connection_->encryption_level());

  EXPECT_CALL(*connection_, SendCryptoData(ENCRYPTION_ZERO_RTT, 350, 1000))
      .WillOnce(Return(350));
  stream_->WriteBufferedCryptoFrames();
  EXPECT_FALSE(stream_->HasBufferedCryptoFrames());
}

TEST_F(QuicCryptoStreamTest, LimitBufferedCryptoData) {
  if (!QuicVersionUsesCryptoFrames(connection_->transport_version())) {
    return;
  }

  EXPECT_CALL(*connection_,
              CloseConnection(QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA, _, _));
  std::string large_frame(2 * GetQuicFlag(FLAGS_quic_max_buffered_crypto_bytes),
                          'a');

  // Set offset to 1 so that we guarantee the data gets buffered instead of
  // immediately processed.
  QuicStreamOffset offset = 1;
  stream_->OnCryptoFrame(
      QuicCryptoFrame(ENCRYPTION_INITIAL, offset, large_frame));
}

}  // namespace
}  // namespace test
}  // namespace quic
