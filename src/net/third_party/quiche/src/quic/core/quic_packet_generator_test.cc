// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_generator.h"

#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_random.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_framer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_packet_creator_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_packet_generator_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_data_producer.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_quic_framer.h"

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockDelegate : public QuicPacketGenerator::DelegateInterface {
 public:
  MockDelegate() {}
  MockDelegate(const MockDelegate&) = delete;
  MockDelegate& operator=(const MockDelegate&) = delete;
  ~MockDelegate() override {}

  MOCK_METHOD2(ShouldGeneratePacket,
               bool(HasRetransmittableData retransmittable,
                    IsHandshake handshake));
  MOCK_METHOD0(MaybeBundleAckOpportunistically, const QuicFrames());
  MOCK_METHOD0(GetPacketBuffer, char*());
  MOCK_METHOD1(OnSerializedPacket, void(SerializedPacket* packet));
  MOCK_METHOD2(OnUnrecoverableError, void(QuicErrorCode, const std::string&));

  void SetCanWriteAnything() {
    EXPECT_CALL(*this, ShouldGeneratePacket(_, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(*this, ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA, _))
        .WillRepeatedly(Return(true));
  }

  void SetCanNotWrite() {
    EXPECT_CALL(*this, ShouldGeneratePacket(_, _))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*this, ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA, _))
        .WillRepeatedly(Return(false));
  }

  // Use this when only ack frames should be allowed to be written.
  void SetCanWriteOnlyNonRetransmittable() {
    EXPECT_CALL(*this, ShouldGeneratePacket(_, _))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*this, ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA, _))
        .WillRepeatedly(Return(true));
  }
};

// Simple struct for describing the contents of a packet.
// Useful in conjunction with a SimpleQuicFrame for validating that a packet
// contains the expected frames.
struct PacketContents {
  PacketContents()
      : num_ack_frames(0),
        num_connection_close_frames(0),
        num_goaway_frames(0),
        num_rst_stream_frames(0),
        num_stop_waiting_frames(0),
        num_stream_frames(0),
        num_crypto_frames(0),
        num_ping_frames(0),
        num_mtu_discovery_frames(0),
        num_padding_frames(0) {}

  size_t num_ack_frames;
  size_t num_connection_close_frames;
  size_t num_goaway_frames;
  size_t num_rst_stream_frames;
  size_t num_stop_waiting_frames;
  size_t num_stream_frames;
  size_t num_crypto_frames;
  size_t num_ping_frames;
  size_t num_mtu_discovery_frames;
  size_t num_padding_frames;
};

}  // namespace

class TestPacketGenerator : public QuicPacketGenerator {
 public:
  TestPacketGenerator(QuicConnectionId connection_id,
                      QuicFramer* framer,
                      QuicRandom* random_generator,
                      DelegateInterface* delegate,
                      SimpleDataProducer* producer)
      : QuicPacketGenerator(connection_id, framer, random_generator, delegate),
        ack_frame_(InitAckFrame(1)),
        delegate_(static_cast<MockDelegate*>(delegate)),
        producer_(producer) {}

  bool ConsumeRetransmittableControlFrame(const QuicFrame& frame,
                                          bool bundle_ack) {
    if (!QuicPacketGeneratorPeer::GetPacketCreator(this)->has_ack()) {
      QuicFrames frames;
      if (bundle_ack) {
        frames.push_back(QuicFrame(&ack_frame_));
      }
      if (delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                          NOT_HANDSHAKE)) {
        EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically())
            .WillOnce(Return(frames));
      }
    }
    return QuicPacketGenerator::ConsumeRetransmittableControlFrame(frame);
  }

  QuicConsumedData ConsumeDataFastPath(QuicStreamId id,
                                       const struct iovec* iov,
                                       int iov_count,
                                       size_t total_length,
                                       QuicStreamOffset offset,
                                       bool fin) {
    // Save data before data is consumed.
    if (total_length > 0) {
      producer_->SaveStreamData(id, iov, iov_count, 0, total_length);
    }
    return QuicPacketGenerator::ConsumeDataFastPath(id, total_length, offset,
                                                    fin, 0);
  }

  QuicConsumedData ConsumeData(QuicStreamId id,
                               const struct iovec* iov,
                               int iov_count,
                               size_t total_length,
                               QuicStreamOffset offset,
                               StreamSendingState state) {
    // Save data before data is consumed.
    if (total_length > 0) {
      producer_->SaveStreamData(id, iov, iov_count, 0, total_length);
    }
    if (!QuicPacketGeneratorPeer::GetPacketCreator(this)->has_ack() &&
        delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                        NOT_HANDSHAKE)) {
      EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically()).Times(1);
    }
    return QuicPacketGenerator::ConsumeData(id, total_length, offset, state);
  }

  MessageStatus AddMessageFrame(QuicMessageId message_id,
                                QuicMemSliceSpan message) {
    if (!QuicPacketGeneratorPeer::GetPacketCreator(this)->has_ack() &&
        delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                        NOT_HANDSHAKE)) {
      EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically()).Times(1);
    }
    return QuicPacketGenerator::AddMessageFrame(message_id, message);
  }

  size_t ConsumeCryptoData(EncryptionLevel level,
                           QuicStringPiece data,
                           QuicStreamOffset offset) {
    producer_->SaveCryptoData(level, offset, data);
    if (!QuicPacketGeneratorPeer::GetPacketCreator(this)->has_ack() &&
        delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                        NOT_HANDSHAKE)) {
      EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically()).Times(1);
    }
    return QuicPacketGenerator::ConsumeCryptoData(level, data.length(), offset);
  }

  QuicAckFrame ack_frame_;
  MockDelegate* delegate_;
  SimpleDataProducer* producer_;
};

class QuicPacketGeneratorTest : public QuicTest {
 public:
  QuicPacketGeneratorTest()
      : framer_(AllSupportedVersions(),
                QuicTime::Zero(),
                Perspective::IS_CLIENT,
                kQuicDefaultConnectionIdLength),
        generator_(TestConnectionId(),
                   &framer_,
                   &random_generator_,
                   &delegate_,
                   &producer_),
        creator_(QuicPacketGeneratorPeer::GetPacketCreator(&generator_)),
        ack_frame_(InitAckFrame(1)) {
    EXPECT_CALL(delegate_, GetPacketBuffer()).WillRepeatedly(Return(nullptr));
    creator_->SetEncrypter(
        ENCRYPTION_FORWARD_SECURE,
        QuicMakeUnique<NullEncrypter>(Perspective::IS_CLIENT));
    creator_->set_encryption_level(ENCRYPTION_FORWARD_SECURE);
    framer_.set_data_producer(&producer_);
    if (simple_framer_.framer()->version().KnowsWhichDecrypterToUse()) {
      simple_framer_.framer()->InstallDecrypter(
          ENCRYPTION_FORWARD_SECURE,
          QuicMakeUnique<NullDecrypter>(Perspective::IS_SERVER));
    }
    generator_.AttachPacketFlusher();
  }

  ~QuicPacketGeneratorTest() override {
    for (SerializedPacket& packet : packets_) {
      delete[] packet.encrypted_buffer;
      ClearSerializedPacket(&packet);
    }
  }

  void SavePacket(SerializedPacket* packet) {
    packet->encrypted_buffer = CopyBuffer(*packet);
    packets_.push_back(*packet);
    packet->encrypted_buffer = nullptr;
    packet->retransmittable_frames.clear();
  }

 protected:
  QuicRstStreamFrame* CreateRstStreamFrame() {
    return new QuicRstStreamFrame(1, 1, QUIC_STREAM_NO_ERROR, 0);
  }

  QuicGoAwayFrame* CreateGoAwayFrame() {
    return new QuicGoAwayFrame(2, QUIC_NO_ERROR, 1, std::string());
  }

  void CheckPacketContains(const PacketContents& contents,
                           size_t packet_index) {
    ASSERT_GT(packets_.size(), packet_index);
    const SerializedPacket& packet = packets_[packet_index];
    size_t num_retransmittable_frames =
        contents.num_connection_close_frames + contents.num_goaway_frames +
        contents.num_rst_stream_frames + contents.num_stream_frames +
        contents.num_crypto_frames + contents.num_ping_frames;
    size_t num_frames =
        contents.num_ack_frames + contents.num_stop_waiting_frames +
        contents.num_mtu_discovery_frames + contents.num_padding_frames +
        num_retransmittable_frames;

    if (num_retransmittable_frames == 0) {
      ASSERT_TRUE(packet.retransmittable_frames.empty());
    } else {
      ASSERT_FALSE(packet.retransmittable_frames.empty());
      EXPECT_EQ(num_retransmittable_frames,
                packet.retransmittable_frames.size());
    }

    ASSERT_TRUE(packet.encrypted_buffer != nullptr);
    ASSERT_TRUE(simple_framer_.ProcessPacket(
        QuicEncryptedPacket(packet.encrypted_buffer, packet.encrypted_length)));
    size_t num_padding_frames = 0;
    if (contents.num_padding_frames == 0) {
      num_padding_frames = simple_framer_.padding_frames().size();
    }
    EXPECT_EQ(num_frames + num_padding_frames, simple_framer_.num_frames());
    EXPECT_EQ(contents.num_ack_frames, simple_framer_.ack_frames().size());
    EXPECT_EQ(contents.num_connection_close_frames,
              simple_framer_.connection_close_frames().size());
    EXPECT_EQ(contents.num_goaway_frames,
              simple_framer_.goaway_frames().size());
    EXPECT_EQ(contents.num_rst_stream_frames,
              simple_framer_.rst_stream_frames().size());
    EXPECT_EQ(contents.num_stream_frames,
              simple_framer_.stream_frames().size());
    EXPECT_EQ(contents.num_crypto_frames,
              simple_framer_.crypto_frames().size());
    EXPECT_EQ(contents.num_stop_waiting_frames,
              simple_framer_.stop_waiting_frames().size());
    if (contents.num_padding_frames != 0) {
      EXPECT_EQ(contents.num_padding_frames,
                simple_framer_.padding_frames().size());
    }

    // From the receiver's perspective, MTU discovery frames are ping frames.
    EXPECT_EQ(contents.num_ping_frames + contents.num_mtu_discovery_frames,
              simple_framer_.ping_frames().size());
  }

  void CheckPacketHasSingleStreamFrame(size_t packet_index) {
    ASSERT_GT(packets_.size(), packet_index);
    const SerializedPacket& packet = packets_[packet_index];
    ASSERT_FALSE(packet.retransmittable_frames.empty());
    EXPECT_EQ(1u, packet.retransmittable_frames.size());
    ASSERT_TRUE(packet.encrypted_buffer != nullptr);
    ASSERT_TRUE(simple_framer_.ProcessPacket(
        QuicEncryptedPacket(packet.encrypted_buffer, packet.encrypted_length)));
    EXPECT_EQ(1u, simple_framer_.num_frames());
    EXPECT_EQ(1u, simple_framer_.stream_frames().size());
  }

  void CheckAllPacketsHaveSingleStreamFrame() {
    for (size_t i = 0; i < packets_.size(); i++) {
      CheckPacketHasSingleStreamFrame(i);
    }
  }

  void CreateData(size_t len) {
    data_array_.reset(new char[len]);
    memset(data_array_.get(), '?', len);
    iov_.iov_base = data_array_.get();
    iov_.iov_len = len;
  }

  QuicFramer framer_;
  MockRandom random_generator_;
  StrictMock<MockDelegate> delegate_;
  TestPacketGenerator generator_;
  QuicPacketCreator* creator_;
  SimpleQuicFramer simple_framer_;
  std::vector<SerializedPacket> packets_;
  QuicAckFrame ack_frame_;
  struct iovec iov_;
  SimpleBufferAllocator allocator_;

 private:
  std::unique_ptr<char[]> data_array_;
  SimpleDataProducer producer_;
};

class MockDebugDelegate : public QuicPacketCreator::DebugDelegate {
 public:
  MOCK_METHOD1(OnFrameAddedToPacket, void(const QuicFrame&));
};

TEST_F(QuicPacketGeneratorTest, AddControlFrame_NotWritable) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/false);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
  delete rst_frame;
}

TEST_F(QuicPacketGeneratorTest, AddControlFrame_OnlyAckWritable) {
  delegate_.SetCanWriteOnlyNonRetransmittable();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/false);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
  delete rst_frame;
}

TEST_F(QuicPacketGeneratorTest, AddControlFrame_WritableAndShouldNotFlush) {
  delegate_.SetCanWriteAnything();

  generator_.ConsumeRetransmittableControlFrame(
      QuicFrame(CreateRstStreamFrame()),
      /*bundle_ack=*/false);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());
}

TEST_F(QuicPacketGeneratorTest, AddControlFrame_NotWritableBatchThenFlush) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/false);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
  delete rst_frame;
}

TEST_F(QuicPacketGeneratorTest, AddControlFrame_WritableAndShouldFlush) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  generator_.ConsumeRetransmittableControlFrame(
      QuicFrame(CreateRstStreamFrame()),
      /*bundle_ack=*/false);
  generator_.Flush();
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_rst_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketGeneratorTest, ConsumeCryptoData) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  std::string data = "crypto data";
  size_t consumed_bytes =
      generator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data, 0);
  generator_.Flush();
  EXPECT_EQ(data.length(), consumed_bytes);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_crypto_frames = 1;
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketGeneratorTest, ConsumeData_NotWritable) {
  delegate_.SetCanNotWrite();

  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  EXPECT_EQ(0u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
}

TEST_F(QuicPacketGeneratorTest, ConsumeData_WritableAndShouldNotFlush) {
  delegate_.SetCanWriteAnything();

  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());
}

TEST_F(QuicPacketGeneratorTest, ConsumeData_WritableAndShouldFlush) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  generator_.Flush();
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

// Test the behavior of ConsumeData when the data consumed is for the crypto
// handshake stream.  Ensure that the packet is always sent and padded even if
// the generator operates in batch mode.
TEST_F(QuicPacketGeneratorTest, ConsumeData_Handshake) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  std::string data = "foo bar";
  MakeIOVector(data, &iov_);
  size_t consumed_bytes = 0;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    consumed_bytes = generator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data, 0);
  } else {
    consumed_bytes =
        generator_
            .ConsumeData(
                QuicUtils::GetCryptoStreamId(framer_.transport_version()),
                &iov_, 1u, iov_.iov_len, 0, NO_FIN)
            .bytes_consumed;
  }
  EXPECT_EQ(7u, consumed_bytes);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    contents.num_crypto_frames = 1;
  } else {
    contents.num_stream_frames = 1;
  }
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);

  ASSERT_EQ(1u, packets_.size());
  ASSERT_EQ(kDefaultMaxPacketSize, generator_.GetCurrentMaxPacketLength());
  EXPECT_EQ(kDefaultMaxPacketSize, packets_[0].encrypted_length);
}

// Test the behavior of ConsumeData when the data is for the crypto handshake
// stream, but padding is disabled.
TEST_F(QuicPacketGeneratorTest, ConsumeData_Handshake_PaddingDisabled) {
  generator_.set_fully_pad_crypto_hadshake_packets(false);

  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  std::string data = "foo";
  MakeIOVector(data, &iov_);
  size_t bytes_consumed = 0;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    bytes_consumed = generator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data, 0);
  } else {
    bytes_consumed =
        generator_
            .ConsumeData(
                QuicUtils::GetCryptoStreamId(framer_.transport_version()),
                &iov_, 1u, iov_.iov_len, 0, NO_FIN)
            .bytes_consumed;
  }
  EXPECT_EQ(3u, bytes_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    contents.num_crypto_frames = 1;
  } else {
    contents.num_stream_frames = 1;
  }
  contents.num_padding_frames = 0;
  CheckPacketContains(contents, 0);

  ASSERT_EQ(1u, packets_.size());

  // Packet is not fully padded, but we want to future packets to be larger.
  ASSERT_EQ(kDefaultMaxPacketSize, generator_.GetCurrentMaxPacketLength());
  size_t expected_packet_length = 27;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    // The framing of CRYPTO frames is slightly different than that of stream
    // frames, so the expected packet length differs slightly.
    expected_packet_length = 28;
  }
  if (framer_.version().HasHeaderProtection()) {
    expected_packet_length = 29;
  }
  EXPECT_EQ(expected_packet_length, packets_[0].encrypted_length);
}

TEST_F(QuicPacketGeneratorTest, ConsumeData_EmptyData) {
  delegate_.SetCanWriteAnything();

  EXPECT_QUIC_BUG(generator_.ConsumeData(QuicUtils::GetHeadersStreamId(
                                             framer_.transport_version()),
                                         nullptr, 0, 0, 0, NO_FIN),
                  "Attempt to consume empty data without FIN.");
}

TEST_F(QuicPacketGeneratorTest,
       ConsumeDataMultipleTimes_WritableAndShouldNotFlush) {
  delegate_.SetCanWriteAnything();

  MakeIOVector("foo", &iov_);
  generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  MakeIOVector("quux", &iov_);
  QuicConsumedData consumed =
      generator_.ConsumeData(3, &iov_, 1u, iov_.iov_len, 3, NO_FIN);
  EXPECT_EQ(4u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());
}

TEST_F(QuicPacketGeneratorTest, ConsumeData_BatchOperations) {
  delegate_.SetCanWriteAnything();

  MakeIOVector("foo", &iov_);
  generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  MakeIOVector("quux", &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 3, NO_FIN);
  EXPECT_EQ(4u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  // Now both frames will be flushed out.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  generator_.Flush();
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 2;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketGeneratorTest, ConsumeData_FramesPreviouslyQueued) {
  // Set the packet size be enough for two stream frames with 0 stream offset,
  // but not enough for a stream frame of 0 offset and one with non-zero offset.
  size_t length =
      NullEncrypter(Perspective::IS_CLIENT).GetCiphertextSize(0) +
      GetPacketHeaderSize(
          framer_.transport_version(),
          creator_->GetDestinationConnectionIdLength(),
          creator_->GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(creator_)) +
      // Add an extra 3 bytes for the payload and 1 byte so
      // BytesFree is larger than the GetMinStreamFrameSize.
      QuicFramer::GetMinStreamFrameSize(framer_.transport_version(), 1, 0,
                                        false, 3) +
      3 +
      QuicFramer::GetMinStreamFrameSize(framer_.transport_version(), 1, 0, true,
                                        1) +
      1;
  generator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  {
    InSequence dummy;
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  }
  // Queue enough data to prevent a stream frame with a non-zero offset from
  // fitting.
  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, NO_FIN);
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  // This frame will not fit with the existing frame, causing the queued frame
  // to be serialized, and it will be added to a new open packet.
  MakeIOVector("bar", &iov_);
  consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 3, FIN);
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  creator_->Flush();
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
  CheckPacketContains(contents, 1);
}

TEST_F(QuicPacketGeneratorTest, ConsumeDataFastPath) {
  delegate_.SetCanWriteAnything();
  generator_.SetCanSetTransmissionType(true);
  generator_.SetTransmissionType(LOSS_RETRANSMISSION);

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  QuicConsumedData consumed = generator_.ConsumeDataFastPath(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, true);
  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
  EXPECT_FALSE(packets_.empty());
  SerializedPacket packet = packets_.back();
  EXPECT_TRUE(!packet.retransmittable_frames.empty());
  EXPECT_EQ(LOSS_RETRANSMISSION, packet.transmission_type);
  EXPECT_EQ(STREAM_FRAME, packet.retransmittable_frames.front().type);
  const QuicStreamFrame& stream_frame =
      packet.retransmittable_frames.front().stream_frame;
  EXPECT_EQ(10000u, stream_frame.data_length + stream_frame.offset);
}

TEST_F(QuicPacketGeneratorTest, ConsumeDataLarge) {
  delegate_.SetCanWriteAnything();

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
  EXPECT_FALSE(packets_.empty());
  SerializedPacket packet = packets_.back();
  EXPECT_TRUE(!packet.retransmittable_frames.empty());
  EXPECT_EQ(STREAM_FRAME, packet.retransmittable_frames.front().type);
  const QuicStreamFrame& stream_frame =
      packet.retransmittable_frames.front().stream_frame;
  EXPECT_EQ(10000u, stream_frame.data_length + stream_frame.offset);
}

TEST_F(QuicPacketGeneratorTest, ConsumeDataLargeSendAckFalse) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool success =
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/true);
  EXPECT_FALSE(success);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  delegate_.SetCanWriteAnything();

  generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                /*bundle_ack=*/false);

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  generator_.ConsumeRetransmittableControlFrame(
      QuicFrame(CreateRstStreamFrame()),
      /*bundle_ack=*/true);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  generator_.Flush();

  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  EXPECT_FALSE(packets_.empty());
  SerializedPacket packet = packets_.back();
  EXPECT_TRUE(!packet.retransmittable_frames.empty());
  EXPECT_EQ(STREAM_FRAME, packet.retransmittable_frames.front().type);
  const QuicStreamFrame& stream_frame =
      packet.retransmittable_frames.front().stream_frame;
  EXPECT_EQ(10000u, stream_frame.data_length + stream_frame.offset);
}

TEST_F(QuicPacketGeneratorTest, ConsumeDataLargeSendAckTrue) {
  if (VersionHasIetfInvariantHeader(framer_.transport_version())) {
    return;
  }
  delegate_.SetCanNotWrite();
  delegate_.SetCanWriteAnything();

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  generator_.Flush();

  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  EXPECT_FALSE(packets_.empty());
  SerializedPacket packet = packets_.back();
  EXPECT_TRUE(!packet.retransmittable_frames.empty());
  EXPECT_EQ(STREAM_FRAME, packet.retransmittable_frames.front().type);
  const QuicStreamFrame& stream_frame =
      packet.retransmittable_frames.front().stream_frame;
  EXPECT_EQ(10000u, stream_frame.data_length + stream_frame.offset);
}

TEST_F(QuicPacketGeneratorTest, NotWritableThenBatchOperations) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/true);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
  EXPECT_FALSE(generator_.HasPendingStreamFramesOfStream(3));

  delegate_.SetCanWriteAnything();

  EXPECT_TRUE(
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/false));
  // Send some data and a control frame
  MakeIOVector("quux", &iov_);
  generator_.ConsumeData(3, &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  if (!VersionHasIetfQuicFrames(framer_.transport_version())) {
    generator_.ConsumeRetransmittableControlFrame(
        QuicFrame(CreateGoAwayFrame()),
        /*bundle_ack=*/false);
  }
  EXPECT_TRUE(generator_.HasPendingStreamFramesOfStream(3));

  // All five frames will be flushed out in a single packet.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  generator_.Flush();
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
  EXPECT_FALSE(generator_.HasPendingStreamFramesOfStream(3));

  PacketContents contents;
  // ACK will be flushed by connection.
  contents.num_ack_frames = 0;
  if (!VersionHasIetfQuicFrames(framer_.transport_version())) {
    contents.num_goaway_frames = 1;
  } else {
    contents.num_goaway_frames = 0;
  }
  contents.num_rst_stream_frames = 1;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketGeneratorTest, NotWritableThenBatchOperations2) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool success =
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/true);
  EXPECT_FALSE(success);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  delegate_.SetCanWriteAnything();

  {
    InSequence dummy;
    // All five frames will be flushed out in a single packet
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  }
  EXPECT_TRUE(
      generator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                    /*bundle_ack=*/false));
  // Send enough data to exceed one packet
  size_t data_len = kDefaultMaxPacketSize + 100;
  CreateData(data_len);
  QuicConsumedData consumed =
      generator_.ConsumeData(3, &iov_, 1u, iov_.iov_len, 0, FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  if (!VersionHasIetfQuicFrames(framer_.transport_version())) {
    generator_.ConsumeRetransmittableControlFrame(
        QuicFrame(CreateGoAwayFrame()),
        /*bundle_ack=*/false);
  }

  generator_.Flush();
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // The first packet should have the queued data and part of the stream data.
  PacketContents contents;
  // ACK will be sent by connection.
  contents.num_ack_frames = 0;
  contents.num_rst_stream_frames = 1;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);

  // The second should have the remainder of the stream data.
  PacketContents contents2;
  if (!VersionHasIetfQuicFrames(framer_.transport_version())) {
    contents2.num_goaway_frames = 1;
  } else {
    contents2.num_goaway_frames = 0;
  }
  contents2.num_stream_frames = 1;
  CheckPacketContains(contents2, 1);
}

// Regression test of b/120493795.
TEST_F(QuicPacketGeneratorTest, PacketTransmissionType) {
  delegate_.SetCanWriteAnything();
  generator_.SetCanSetTransmissionType(true);

  // The first ConsumeData will fill the packet without flush.
  generator_.SetTransmissionType(LOSS_RETRANSMISSION);

  size_t data_len = 1324;
  CreateData(data_len);
  QuicStreamId stream1_id =
      QuicUtils::GetHeadersStreamId(framer_.transport_version());
  QuicConsumedData consumed =
      generator_.ConsumeData(stream1_id, &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  ASSERT_EQ(0u, creator_->BytesFree())
      << "Test setup failed: Please increase data_len to "
      << data_len + creator_->BytesFree() << " bytes.";

  // The second ConsumeData can not be added to the packet and will flush.
  generator_.SetTransmissionType(NOT_RETRANSMISSION);

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  QuicStreamId stream2_id = stream1_id + 4;

  consumed =
      generator_.ConsumeData(stream2_id, &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);

  // Ensure the packet is successfully created.
  ASSERT_EQ(1u, packets_.size());
  ASSERT_TRUE(packets_[0].encrypted_buffer);
  ASSERT_EQ(1u, packets_[0].retransmittable_frames.size());
  EXPECT_EQ(stream1_id,
            packets_[0].retransmittable_frames[0].stream_frame.stream_id);

  // Since the second frame was not added, the packet's transmission type
  // should be the first frame's type.
  EXPECT_EQ(packets_[0].transmission_type, LOSS_RETRANSMISSION);
}

TEST_F(QuicPacketGeneratorTest, TestConnectionIdLength) {
  QuicFramerPeer::SetPerspective(&framer_, Perspective::IS_SERVER);
  generator_.SetServerConnectionIdLength(0);
  EXPECT_EQ(PACKET_0BYTE_CONNECTION_ID,
            creator_->GetDestinationConnectionIdLength());

  for (size_t i = 1; i < 10; i++) {
    generator_.SetServerConnectionIdLength(i);
    if (VersionHasIetfInvariantHeader(framer_.transport_version())) {
      EXPECT_EQ(PACKET_0BYTE_CONNECTION_ID,
                creator_->GetDestinationConnectionIdLength());
    } else {
      EXPECT_EQ(PACKET_8BYTE_CONNECTION_ID,
                creator_->GetDestinationConnectionIdLength());
    }
  }
}

// Test whether SetMaxPacketLength() works in the situation when the queue is
// empty, and we send three packets worth of data.
TEST_F(QuicPacketGeneratorTest, SetMaxPacketLength_Initial) {
  delegate_.SetCanWriteAnything();

  // Send enough data for three packets.
  size_t data_len = 3 * kDefaultMaxPacketSize + 1;
  size_t packet_len = kDefaultMaxPacketSize + 100;
  ASSERT_LE(packet_len, kMaxOutgoingPacketSize);
  generator_.SetMaxPacketLength(packet_len);
  EXPECT_EQ(packet_len, generator_.GetCurrentMaxPacketLength());

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .Times(3)
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  CreateData(data_len);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len,
      /*offset=*/0, FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // We expect three packets, and first two of them have to be of packet_len
  // size.  We check multiple packets (instead of just one) because we want to
  // ensure that |max_packet_length_| does not get changed incorrectly by the
  // generator after first packet is serialized.
  ASSERT_EQ(3u, packets_.size());
  EXPECT_EQ(packet_len, packets_[0].encrypted_length);
  EXPECT_EQ(packet_len, packets_[1].encrypted_length);
  CheckAllPacketsHaveSingleStreamFrame();
}

// Test whether SetMaxPacketLength() works in the situation when we first write
// data, then change packet size, then write data again.
TEST_F(QuicPacketGeneratorTest, SetMaxPacketLength_Middle) {
  delegate_.SetCanWriteAnything();

  // We send enough data to overflow default packet length, but not the altered
  // one.
  size_t data_len = kDefaultMaxPacketSize;
  size_t packet_len = kDefaultMaxPacketSize + 100;
  ASSERT_LE(packet_len, kMaxOutgoingPacketSize);

  // We expect to see three packets in total.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .Times(3)
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  // Send two packets before packet size change.
  CreateData(data_len);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len,
      /*offset=*/0, NO_FIN);
  generator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // Make sure we already have two packets.
  ASSERT_EQ(2u, packets_.size());

  // Increase packet size.
  generator_.SetMaxPacketLength(packet_len);
  EXPECT_EQ(packet_len, generator_.GetCurrentMaxPacketLength());

  // Send a packet after packet size change.
  CreateData(data_len);
  generator_.AttachPacketFlusher();
  consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, data_len, FIN);
  generator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // We expect first data chunk to get fragmented, but the second one to fit
  // into a single packet.
  ASSERT_EQ(3u, packets_.size());
  EXPECT_EQ(kDefaultMaxPacketSize, packets_[0].encrypted_length);
  EXPECT_LE(kDefaultMaxPacketSize, packets_[2].encrypted_length);
  CheckAllPacketsHaveSingleStreamFrame();
}

// Test whether SetMaxPacketLength() works correctly when we force the change of
// the packet size in the middle of the batched packet.
TEST_F(QuicPacketGeneratorTest, SetMaxPacketLength_MidpacketFlush) {
  delegate_.SetCanWriteAnything();

  size_t first_write_len = kDefaultMaxPacketSize / 2;
  size_t packet_len = kDefaultMaxPacketSize + 100;
  size_t second_write_len = packet_len + 1;
  ASSERT_LE(packet_len, kMaxOutgoingPacketSize);

  // First send half of the packet worth of data.  We are in the batch mode, so
  // should not cause packet serialization.
  CreateData(first_write_len);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len,
      /*offset=*/0, NO_FIN);
  EXPECT_EQ(first_write_len, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  // Make sure we have no packets so far.
  ASSERT_EQ(0u, packets_.size());

  // Expect a packet to be flushed.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  // Increase packet size after flushing all frames.
  // Ensure it's immediately enacted.
  generator_.FlushAllQueuedFrames();
  generator_.SetMaxPacketLength(packet_len);
  EXPECT_EQ(packet_len, generator_.GetCurrentMaxPacketLength());
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // We expect to see exactly one packet serialized after that, because we send
  // a value somewhat exceeding new max packet size, and the tail data does not
  // get serialized because we are still in the batch mode.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  // Send a more than a packet worth of data to the same stream.  This should
  // trigger serialization of one packet, and queue another one.
  CreateData(second_write_len);
  consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len,
      /*offset=*/first_write_len, FIN);
  EXPECT_EQ(second_write_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  // We expect the first packet to be underfilled, and the second packet be up
  // to the new max packet size.
  ASSERT_EQ(2u, packets_.size());
  EXPECT_GT(kDefaultMaxPacketSize, packets_[0].encrypted_length);
  EXPECT_EQ(packet_len, packets_[1].encrypted_length);

  CheckAllPacketsHaveSingleStreamFrame();
}

// Test sending a connectivity probing packet.
TEST_F(QuicPacketGeneratorTest, GenerateConnectivityProbingPacket) {
  delegate_.SetCanWriteAnything();

  OwningSerializedPacketPointer probing_packet;
  if (VersionHasIetfQuicFrames(framer_.transport_version())) {
    QuicPathFrameBuffer payload = {
        {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xfe}};
    probing_packet =
        generator_.SerializePathChallengeConnectivityProbingPacket(&payload);
  } else {
    probing_packet = generator_.SerializeConnectivityProbingPacket();
  }

  ASSERT_TRUE(simple_framer_.ProcessPacket(QuicEncryptedPacket(
      probing_packet->encrypted_buffer, probing_packet->encrypted_length)));

  EXPECT_EQ(2u, simple_framer_.num_frames());
  if (VersionHasIetfQuicFrames(framer_.transport_version())) {
    EXPECT_EQ(1u, simple_framer_.path_challenge_frames().size());
  } else {
    EXPECT_EQ(1u, simple_framer_.ping_frames().size());
  }
  EXPECT_EQ(1u, simple_framer_.padding_frames().size());
}

// Test sending an MTU probe, without any surrounding data.
TEST_F(QuicPacketGeneratorTest, GenerateMtuDiscoveryPacket_Simple) {
  delegate_.SetCanWriteAnything();

  const size_t target_mtu = kDefaultMaxPacketSize + 100;
  static_assert(target_mtu < kMaxOutgoingPacketSize,
                "The MTU probe used by the test exceeds maximum packet size");

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  generator_.GenerateMtuDiscoveryPacket(target_mtu);

  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());
  ASSERT_EQ(1u, packets_.size());
  EXPECT_EQ(target_mtu, packets_[0].encrypted_length);

  PacketContents contents;
  contents.num_mtu_discovery_frames = 1;
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);
}

// Test sending an MTU probe.  Surround it with data, to ensure that it resets
// the MTU to the value before the probe was sent.
TEST_F(QuicPacketGeneratorTest, GenerateMtuDiscoveryPacket_SurroundedByData) {
  delegate_.SetCanWriteAnything();

  const size_t target_mtu = kDefaultMaxPacketSize + 100;
  static_assert(target_mtu < kMaxOutgoingPacketSize,
                "The MTU probe used by the test exceeds maximum packet size");

  // Send enough data so it would always cause two packets to be sent.
  const size_t data_len = target_mtu + 1;

  // Send a total of five packets: two packets before the probe, the probe
  // itself, and two packets after the probe.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .Times(5)
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  // Send data before the MTU probe.
  CreateData(data_len);
  QuicConsumedData consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len,
      /*offset=*/0, NO_FIN);
  generator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // Send the MTU probe.
  generator_.GenerateMtuDiscoveryPacket(target_mtu);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  // Send data after the MTU probe.
  CreateData(data_len);
  generator_.AttachPacketFlusher();
  consumed = generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len,
      /*offset=*/data_len, FIN);
  generator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  ASSERT_EQ(5u, packets_.size());
  EXPECT_EQ(kDefaultMaxPacketSize, packets_[0].encrypted_length);
  EXPECT_EQ(target_mtu, packets_[2].encrypted_length);
  EXPECT_EQ(kDefaultMaxPacketSize, packets_[3].encrypted_length);

  PacketContents probe_contents;
  probe_contents.num_mtu_discovery_frames = 1;
  probe_contents.num_padding_frames = 1;

  CheckPacketHasSingleStreamFrame(0);
  CheckPacketHasSingleStreamFrame(1);
  CheckPacketContains(probe_contents, 2);
  CheckPacketHasSingleStreamFrame(3);
  CheckPacketHasSingleStreamFrame(4);
}

TEST_F(QuicPacketGeneratorTest, DontCrashOnInvalidStopWaiting) {
  if (VersionSupportsMessageFrames(framer_.transport_version())) {
    return;
  }
  // Test added to ensure the generator does not crash when an invalid frame is
  // added.  Because this is an indication of internal programming errors,
  // DFATALs are expected.
  // A 1 byte packet number length can't encode a gap of 1000.
  QuicPacketCreatorPeer::SetPacketNumber(creator_, 1000);

  delegate_.SetCanNotWrite();
  delegate_.SetCanWriteAnything();

  // This will not serialize any packets, because of the invalid frame.
  EXPECT_CALL(delegate_,
              OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET, _));
  EXPECT_QUIC_BUG(generator_.Flush(),
                  "packet_number_length 1 is too small "
                  "for least_unacked_delta: 1001");
}

// Regression test for b/31486443.
TEST_F(QuicPacketGeneratorTest, ConnectionCloseFrameLargerThanPacketSize) {
  delegate_.SetCanWriteAnything();
  char buf[2000] = {};
  QuicStringPiece error_details(buf, 2000);
  QuicConnectionCloseFrame* frame = new QuicConnectionCloseFrame(
      QUIC_PACKET_WRITE_ERROR, std::string(error_details));
  if (VersionHasIetfQuicFrames(framer_.transport_version())) {
    frame->close_type = IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
  }
  generator_.ConsumeRetransmittableControlFrame(QuicFrame(frame),
                                                /*bundle_ack=*/false);
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());
}

TEST_F(QuicPacketGeneratorTest, RandomPaddingAfterFinSingleStreamSinglePacket) {
  const QuicByteCount kStreamFramePayloadSize = 100u;
  char buf[kStreamFramePayloadSize] = {};
  const QuicStreamId kDataStreamId = 5;
  // Set the packet size be enough for one stream frame with 0 stream offset and
  // max size of random padding.
  size_t length =
      NullEncrypter(Perspective::IS_CLIENT).GetCiphertextSize(0) +
      GetPacketHeaderSize(
          framer_.transport_version(),
          creator_->GetDestinationConnectionIdLength(),
          creator_->GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(creator_)) +
      QuicFramer::GetMinStreamFrameSize(
          framer_.transport_version(), kDataStreamId, 0,
          /*last_frame_in_packet=*/false,
          kStreamFramePayloadSize + kMaxNumRandomPaddingBytes) +
      kStreamFramePayloadSize + kMaxNumRandomPaddingBytes;
  generator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  MakeIOVector(QuicStringPiece(buf, kStreamFramePayloadSize), &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      kDataStreamId, &iov_, 1u, iov_.iov_len, 0, FIN_AND_PADDING);
  generator_.Flush();
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  EXPECT_EQ(1u, packets_.size());
  PacketContents contents;
  // The packet has both stream and padding frames.
  contents.num_padding_frames = 1;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketGeneratorTest,
       RandomPaddingAfterFinSingleStreamMultiplePackets) {
  const QuicByteCount kStreamFramePayloadSize = 100u;
  char buf[kStreamFramePayloadSize] = {};
  const QuicStreamId kDataStreamId = 5;
  // Set the packet size be enough for one stream frame with 0 stream offset +
  // 1. One or more packets will accommodate.
  size_t length =
      NullEncrypter(Perspective::IS_CLIENT).GetCiphertextSize(0) +
      GetPacketHeaderSize(
          framer_.transport_version(),
          creator_->GetDestinationConnectionIdLength(),
          creator_->GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(creator_)) +
      QuicFramer::GetMinStreamFrameSize(
          framer_.transport_version(), kDataStreamId, 0,
          /*last_frame_in_packet=*/false, kStreamFramePayloadSize + 1) +
      kStreamFramePayloadSize + 1;
  generator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  MakeIOVector(QuicStringPiece(buf, kStreamFramePayloadSize), &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      kDataStreamId, &iov_, 1u, iov_.iov_len, 0, FIN_AND_PADDING);
  generator_.Flush();
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  EXPECT_LE(1u, packets_.size());
  PacketContents contents;
  // The first packet has both stream and padding frames.
  contents.num_stream_frames = 1;
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);

  for (size_t i = 1; i < packets_.size(); ++i) {
    // Following packets only have paddings.
    contents.num_stream_frames = 0;
    contents.num_padding_frames = 1;
    CheckPacketContains(contents, i);
  }
}

TEST_F(QuicPacketGeneratorTest,
       RandomPaddingAfterFinMultipleStreamsMultiplePackets) {
  const QuicByteCount kStreamFramePayloadSize = 100u;
  char buf[kStreamFramePayloadSize] = {};
  const QuicStreamId kDataStreamId1 = 5;
  const QuicStreamId kDataStreamId2 = 6;
  // Set the packet size be enough for first frame with 0 stream offset + second
  // frame + 1 byte payload. two or more packets will accommodate.
  size_t length =
      NullEncrypter(Perspective::IS_CLIENT).GetCiphertextSize(0) +
      GetPacketHeaderSize(
          framer_.transport_version(),
          creator_->GetDestinationConnectionIdLength(),
          creator_->GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(creator_)) +
      QuicFramer::GetMinStreamFrameSize(
          framer_.transport_version(), kDataStreamId1, 0,
          /*last_frame_in_packet=*/false, kStreamFramePayloadSize) +
      kStreamFramePayloadSize +
      QuicFramer::GetMinStreamFrameSize(framer_.transport_version(),
                                        kDataStreamId1, 0,
                                        /*last_frame_in_packet=*/false, 1) +
      1;
  generator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(Invoke(this, &QuicPacketGeneratorTest::SavePacket));
  MakeIOVector(QuicStringPiece(buf, kStreamFramePayloadSize), &iov_);
  QuicConsumedData consumed = generator_.ConsumeData(
      kDataStreamId1, &iov_, 1u, iov_.iov_len, 0, FIN_AND_PADDING);
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  MakeIOVector(QuicStringPiece(buf, kStreamFramePayloadSize), &iov_);
  consumed = generator_.ConsumeData(kDataStreamId2, &iov_, 1u, iov_.iov_len, 0,
                                    FIN_AND_PADDING);
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  generator_.Flush();
  EXPECT_FALSE(generator_.HasPendingFrames());
  EXPECT_FALSE(generator_.HasRetransmittableFrames());

  EXPECT_LE(2u, packets_.size());
  PacketContents contents;
  // The first packet has two stream frames.
  contents.num_stream_frames = 2;
  CheckPacketContains(contents, 0);

  // The second packet has one stream frame and padding frames.
  contents.num_stream_frames = 1;
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 1);

  for (size_t i = 2; i < packets_.size(); ++i) {
    // Following packets only have paddings.
    contents.num_stream_frames = 0;
    contents.num_padding_frames = 1;
    CheckPacketContains(contents, i);
  }
}

TEST_F(QuicPacketGeneratorTest, AddMessageFrame) {
  if (!VersionSupportsMessageFrames(framer_.transport_version())) {
    return;
  }
  quic::QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketGeneratorTest::SavePacket));

  MakeIOVector("foo", &iov_);
  generator_.ConsumeData(
      QuicUtils::GetHeadersStreamId(framer_.transport_version()), &iov_, 1u,
      iov_.iov_len, 0, FIN);
  EXPECT_EQ(MESSAGE_STATUS_SUCCESS,
            generator_.AddMessageFrame(
                1, MakeSpan(&allocator_, "message", &storage)));
  EXPECT_TRUE(generator_.HasPendingFrames());
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  // Add a message which causes the flush of current packet.
  EXPECT_EQ(
      MESSAGE_STATUS_SUCCESS,
      generator_.AddMessageFrame(
          2, MakeSpan(
                 &allocator_,
                 std::string(generator_.GetCurrentLargestMessagePayload(), 'a'),
                 &storage)));
  EXPECT_TRUE(generator_.HasRetransmittableFrames());

  // Failed to send messages which cannot fit into one packet.
  EXPECT_EQ(
      MESSAGE_STATUS_TOO_LARGE,
      generator_.AddMessageFrame(
          3,
          MakeSpan(&allocator_,
                   std::string(
                       generator_.GetCurrentLargestMessagePayload() + 10, 'a'),
                   &storage)));
}

TEST_F(QuicPacketGeneratorTest, ConnectionId) {
  SetQuicRestartFlag(quic_do_not_override_connection_id, true);
  generator_.SetServerConnectionId(TestConnectionId(0x1337));
  EXPECT_EQ(TestConnectionId(0x1337), creator_->GetDestinationConnectionId());
  EXPECT_EQ(EmptyQuicConnectionId(), creator_->GetSourceConnectionId());
  if (!framer_.version().SupportsClientConnectionIds()) {
    return;
  }
  generator_.SetClientConnectionId(TestConnectionId(0x33));
  EXPECT_EQ(TestConnectionId(0x1337), creator_->GetDestinationConnectionId());
  EXPECT_EQ(TestConnectionId(0x33), creator_->GetSourceConnectionId());
}

}  // namespace test
}  // namespace quic
