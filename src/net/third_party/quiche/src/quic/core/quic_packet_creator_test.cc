// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_pending_retransmission.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_framer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_packet_creator_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_data_producer.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

// Run tests with combinations of {ParsedQuicVersion,
// ToggleVersionSerialization}.
struct TestParams {
  TestParams(ParsedQuicVersion version, bool version_serialization)
      : version(version), version_serialization(version_serialization) {}

  friend std::ostream& operator<<(std::ostream& os, const TestParams& p) {
    os << "{ version: " << ParsedQuicVersionToString(p.version)
       << " include version: " << p.version_serialization << " }";
    return os;
  }

  ParsedQuicVersion version;
  bool version_serialization;
};

// Constructs various test permutations.
std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  ParsedQuicVersionVector all_supported_versions = AllSupportedVersions();
  for (size_t i = 0; i < all_supported_versions.size(); ++i) {
    params.push_back(TestParams(all_supported_versions[i], true));
    params.push_back(TestParams(all_supported_versions[i], false));
  }
  return params;
}

class MockDebugDelegate : public QuicPacketCreator::DebugDelegate {
 public:
  ~MockDebugDelegate() override = default;

  MOCK_METHOD1(OnFrameAddedToPacket, void(const QuicFrame& frame));
};

class TestPacketCreator : public QuicPacketCreator {
 public:
  TestPacketCreator(QuicConnectionId connection_id,
                    QuicFramer* framer,
                    DelegateInterface* delegate,
                    SimpleDataProducer* producer)
      : QuicPacketCreator(connection_id, framer, delegate),
        producer_(producer),
        version_(framer->transport_version()) {}

  bool ConsumeData(QuicStreamId id,
                   const struct iovec* iov,
                   int iov_count,
                   size_t total_length,
                   size_t iov_offset,
                   QuicStreamOffset offset,
                   bool fin,
                   bool needs_full_padding,
                   TransmissionType transmission_type,
                   QuicFrame* frame) {
    // Save data before data is consumed.
    QuicByteCount data_length = total_length - iov_offset;
    if (data_length > 0) {
      producer_->SaveStreamData(id, iov, iov_count, iov_offset, data_length);
    }
    return QuicPacketCreator::ConsumeData(id, data_length - iov_offset, offset,
                                          fin, needs_full_padding,
                                          transmission_type, frame);
  }

  void StopSendingVersion() {
    if (VersionHasIetfInvariantHeader(version_)) {
      set_encryption_level(ENCRYPTION_FORWARD_SECURE);
      return;
    }
    QuicPacketCreator::StopSendingVersion();
  }

  SimpleDataProducer* producer_;
  QuicTransportVersion version_;
};

class QuicPacketCreatorTest : public QuicTestWithParam<TestParams> {
 public:
  void ClearSerializedPacketForTests(SerializedPacket* serialized_packet) {
    if (serialized_packet == nullptr) {
      return;
    }
    ClearSerializedPacket(serialized_packet);
  }

  void SaveSerializedPacket(SerializedPacket* serialized_packet) {
    if (serialized_packet == nullptr) {
      return;
    }
    delete[] serialized_packet_.encrypted_buffer;
    serialized_packet_ = *serialized_packet;
    serialized_packet_.encrypted_buffer = CopyBuffer(*serialized_packet);
    serialized_packet->retransmittable_frames.clear();
  }

  void DeleteSerializedPacket() {
    delete[] serialized_packet_.encrypted_buffer;
    serialized_packet_.encrypted_buffer = nullptr;
    ClearSerializedPacket(&serialized_packet_);
  }

 protected:
  QuicPacketCreatorTest()
      : connection_id_(TestConnectionId(2)),
        server_framer_(SupportedVersions(GetParam().version),
                       QuicTime::Zero(),
                       Perspective::IS_SERVER,
                       connection_id_.length()),
        client_framer_(SupportedVersions(GetParam().version),
                       QuicTime::Zero(),
                       Perspective::IS_CLIENT,
                       connection_id_.length()),
        data_("foo"),
        creator_(connection_id_, &client_framer_, &delegate_, &producer_),
        serialized_packet_(creator_.NoPacket()) {
    QuicPacketCreatorPeer::EnableGetPacketHeaderSizeBugFix(&creator_);
    EXPECT_CALL(delegate_, GetPacketBuffer()).WillRepeatedly(Return(nullptr));
    creator_.SetEncrypter(ENCRYPTION_HANDSHAKE, QuicMakeUnique<NullEncrypter>(
                                                    Perspective::IS_CLIENT));
    creator_.SetEncrypter(ENCRYPTION_ZERO_RTT, QuicMakeUnique<NullEncrypter>(
                                                   Perspective::IS_CLIENT));
    creator_.SetEncrypter(
        ENCRYPTION_FORWARD_SECURE,
        QuicMakeUnique<NullEncrypter>(Perspective::IS_CLIENT));
    client_framer_.set_visitor(&framer_visitor_);
    server_framer_.set_visitor(&framer_visitor_);
    client_framer_.set_data_producer(&producer_);
    if (server_framer_.version().KnowsWhichDecrypterToUse()) {
      server_framer_.InstallDecrypter(
          ENCRYPTION_ZERO_RTT,
          QuicMakeUnique<NullDecrypter>(Perspective::IS_SERVER));
      server_framer_.InstallDecrypter(
          ENCRYPTION_HANDSHAKE,
          QuicMakeUnique<NullDecrypter>(Perspective::IS_SERVER));
      server_framer_.InstallDecrypter(
          ENCRYPTION_FORWARD_SECURE,
          QuicMakeUnique<NullDecrypter>(Perspective::IS_SERVER));
    }
  }

  ~QuicPacketCreatorTest() override {
    delete[] serialized_packet_.encrypted_buffer;
    ClearSerializedPacket(&serialized_packet_);
  }

  SerializedPacket SerializeAllFrames(const QuicFrames& frames) {
    SerializedPacket packet = QuicPacketCreatorPeer::SerializeAllFrames(
        &creator_, frames, buffer_, kMaxOutgoingPacketSize);
    EXPECT_EQ(QuicPacketCreatorPeer::GetEncryptionLevel(&creator_),
              packet.encryption_level);
    return packet;
  }

  void ProcessPacket(const SerializedPacket& packet) {
    QuicEncryptedPacket encrypted_packet(packet.encrypted_buffer,
                                         packet.encrypted_length);
    server_framer_.ProcessPacket(encrypted_packet);
  }

  void CheckStreamFrame(const QuicFrame& frame,
                        QuicStreamId stream_id,
                        const std::string& data,
                        QuicStreamOffset offset,
                        bool fin) {
    EXPECT_EQ(STREAM_FRAME, frame.type);
    EXPECT_EQ(stream_id, frame.stream_frame.stream_id);
    char buf[kMaxOutgoingPacketSize];
    QuicDataWriter writer(kMaxOutgoingPacketSize, buf, HOST_BYTE_ORDER);
    if (frame.stream_frame.data_length > 0) {
      producer_.WriteStreamData(stream_id, frame.stream_frame.offset,
                                frame.stream_frame.data_length, &writer);
    }
    EXPECT_EQ(data, QuicStringPiece(buf, frame.stream_frame.data_length));
    EXPECT_EQ(offset, frame.stream_frame.offset);
    EXPECT_EQ(fin, frame.stream_frame.fin);
  }

  // Returns the number of bytes consumed by the header of packet, including
  // the version.
  size_t GetPacketHeaderOverhead(QuicTransportVersion version) {
    return GetPacketHeaderSize(
        version, creator_.GetDestinationConnectionIdLength(),
        creator_.GetSourceConnectionIdLength(),
        QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
        !kIncludeDiversificationNonce,
        QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
        QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_), 0,
        QuicPacketCreatorPeer::GetLengthLength(&creator_));
  }

  // Returns the number of bytes of overhead that will be added to a packet
  // of maximum length.
  size_t GetEncryptionOverhead() {
    return creator_.max_packet_length() -
           client_framer_.GetMaxPlaintextSize(creator_.max_packet_length());
  }

  // Returns the number of bytes consumed by the non-data fields of a stream
  // frame, assuming it is the last frame in the packet
  size_t GetStreamFrameOverhead(QuicTransportVersion version) {
    return QuicFramer::GetMinStreamFrameSize(
        version, GetNthClientInitiatedStreamId(1), kOffset, true,
        /* data_length= */ 0);
  }

  QuicPendingRetransmission CreateRetransmission(
      const QuicFrames& retransmittable_frames,
      bool has_crypto_handshake,
      int num_padding_bytes,
      EncryptionLevel encryption_level,
      QuicPacketNumberLength packet_number_length) {
    return QuicPendingRetransmission(QuicPacketNumber(1u), NOT_RETRANSMISSION,
                                     retransmittable_frames,
                                     has_crypto_handshake, num_padding_bytes,
                                     encryption_level, packet_number_length);
  }

  bool IsDefaultTestConfiguration() {
    TestParams p = GetParam();
    return p.version == AllSupportedVersions()[0] && p.version_serialization;
  }

  QuicStreamId GetNthClientInitiatedStreamId(int n) const {
    return QuicUtils::GetFirstBidirectionalStreamId(
               creator_.transport_version(), Perspective::IS_CLIENT) +
           n * 2;
  }

  static const QuicStreamOffset kOffset = 0u;

  char buffer_[kMaxOutgoingPacketSize];
  QuicConnectionId connection_id_;
  QuicFrames frames_;
  QuicFramer server_framer_;
  QuicFramer client_framer_;
  StrictMock<MockFramerVisitor> framer_visitor_;
  StrictMock<MockPacketCreatorDelegate> delegate_;
  std::string data_;
  struct iovec iov_;
  TestPacketCreator creator_;
  SerializedPacket serialized_packet_;
  SimpleDataProducer producer_;
  SimpleBufferAllocator allocator_;
};

// Run all packet creator tests with all supported versions of QUIC, and with
// and without version in the packet header, as well as doing a run for each
// length of truncated connection id.
INSTANTIATE_TEST_SUITE_P(QuicPacketCreatorTests,
                         QuicPacketCreatorTest,
                         ::testing::ValuesIn(GetTestParams()));

TEST_P(QuicPacketCreatorTest, SerializeFrames) {
  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);
    frames_.push_back(QuicFrame(new QuicAckFrame(InitAckFrame(1))));
    QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        client_framer_.transport_version(), Perspective::IS_CLIENT);
    if (level != ENCRYPTION_INITIAL && level != ENCRYPTION_HANDSHAKE) {
      frames_.push_back(
          QuicFrame(QuicStreamFrame(stream_id, false, 0u, QuicStringPiece())));
      frames_.push_back(
          QuicFrame(QuicStreamFrame(stream_id, true, 0u, QuicStringPiece())));
    }
    SerializedPacket serialized = SerializeAllFrames(frames_);
    EXPECT_EQ(level, serialized.encryption_level);
    delete frames_[0].ack_frame;
    frames_.clear();

    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnAckFrameStart(_, _))
          .WillOnce(Return(true));
      EXPECT_CALL(framer_visitor_,
                  OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2)))
          .WillOnce(Return(true));
      EXPECT_CALL(framer_visitor_, OnAckFrameEnd(QuicPacketNumber(1)))
          .WillOnce(Return(true));
      if (level != ENCRYPTION_INITIAL && level != ENCRYPTION_HANDSHAKE) {
        EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
        EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
      }
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    ProcessPacket(serialized);
  }
}

TEST_P(QuicPacketCreatorTest, ReserializeFramesWithSequenceNumberLength) {
  if (VersionHasIetfInvariantHeader(client_framer_.transport_version())) {
    creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  }
  // If the original packet number length, the current packet number
  // length, and the configured send packet number length are different, the
  // retransmit must sent with the original length and the others do not change.
  QuicPacketCreatorPeer::SetPacketNumberLength(&creator_,
                                               PACKET_2BYTE_PACKET_NUMBER);
  QuicFrames frames;
  std::string data("a");
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    QuicStreamFrame stream_frame(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        /*fin=*/false, 0u, QuicStringPiece());
    frames.push_back(QuicFrame(stream_frame));
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    frames.push_back(
        QuicFrame(new QuicCryptoFrame(ENCRYPTION_INITIAL, 0, data.length())));
  }
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, true /* has_crypto_handshake */, -1 /* needs full padding */,
      ENCRYPTION_INITIAL, PACKET_4BYTE_PACKET_NUMBER));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);
  // The packet number length is updated after every packet is sent,
  // so there is no need to restore the old length after sending.
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            serialized_packet_.packet_number_length);

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    } else {
      EXPECT_CALL(framer_visitor_, OnCryptoFrame(_));
    }
    EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_);
  DeleteFrames(&frames);
}

TEST_P(QuicPacketCreatorTest, ReserializeCryptoFrameWithForwardSecurity) {
  QuicFrames frames;
  std::string data("a");
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    QuicStreamFrame stream_frame(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        /*fin=*/false, 0u, QuicStringPiece());
    frames.push_back(QuicFrame(stream_frame));
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    frames.push_back(
        QuicFrame(new QuicCryptoFrame(ENCRYPTION_INITIAL, 0, data.length())));
  }
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, true /* has_crypto_handshake */, -1 /* needs full padding */,
      ENCRYPTION_INITIAL,
      QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(ENCRYPTION_INITIAL, serialized_packet_.encryption_level);
  DeleteFrames(&frames);
}

TEST_P(QuicPacketCreatorTest, ReserializeFrameWithForwardSecurity) {
  QuicStreamFrame stream_frame(0u, /*fin=*/false, 0u, QuicStringPiece());
  QuicFrames frames;
  frames.push_back(QuicFrame(stream_frame));
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, false /* has_crypto_handshake */, 0 /* no padding */,
      ENCRYPTION_INITIAL,
      QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(ENCRYPTION_FORWARD_SECURE, serialized_packet_.encryption_level);
}

TEST_P(QuicPacketCreatorTest, ReserializeFramesWithFullPadding) {
  QuicFrame frame;
  std::string data = "fake handshake message data";
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    MakeIOVector(data, &iov_);
    producer_.SaveStreamData(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()), &iov_,
        1u, 0u, iov_.iov_len);
    QuicPacketCreatorPeer::CreateStreamFrame(
        &creator_,
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        iov_.iov_len, 0u, false, &frame);
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    EXPECT_TRUE(QuicPacketCreatorPeer::CreateCryptoFrame(
        &creator_, ENCRYPTION_INITIAL, data.length(), 0, &frame));
  }
  QuicFrames frames;
  frames.push_back(frame);
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, true /* has_crypto_handshake */, -1 /* needs full padding */,
      ENCRYPTION_INITIAL,
      QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.encrypted_length);
  DeleteFrames(&frames);
}

TEST_P(QuicPacketCreatorTest, DoNotRetransmitPendingPadding) {
  QuicFrame frame;
  std::string data = "fake message data";
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    MakeIOVector(data, &iov_);
    producer_.SaveStreamData(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()), &iov_,
        1u, 0u, iov_.iov_len);
    QuicPacketCreatorPeer::CreateStreamFrame(
        &creator_,
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        iov_.iov_len, 0u, false, &frame);
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    EXPECT_TRUE(QuicPacketCreatorPeer::CreateCryptoFrame(
        &creator_, ENCRYPTION_INITIAL, data.length(), 0, &frame));
  }

  const int kNumPaddingBytes1 = 4;
  int packet_size = 0;
  {
    QuicFrames frames;
    frames.push_back(frame);
    char buffer[kMaxOutgoingPacketSize];
    QuicPendingRetransmission retransmission(CreateRetransmission(
        frames, false /* has_crypto_handshake */,
        kNumPaddingBytes1 /* padding bytes */, ENCRYPTION_INITIAL,
        QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    creator_.ReserializeAllFrames(retransmission, buffer,
                                  kMaxOutgoingPacketSize);
    packet_size = serialized_packet_.encrypted_length;
  }

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    if (QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      EXPECT_CALL(framer_visitor_, OnCryptoFrame(_));
    } else {
      EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    }
    // Pending paddings are not retransmitted.
    EXPECT_CALL(framer_visitor_, OnPaddingFrame(_)).Times(0);
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_);

  const int kNumPaddingBytes2 = 44;
  QuicFrames frames;
  frames.push_back(frame);
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, false /* has_crypto_handshake */,
      kNumPaddingBytes2 /* padding bytes */, ENCRYPTION_INITIAL,
      QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);

  EXPECT_EQ(packet_size, serialized_packet_.encrypted_length);
  DeleteFrames(&frames);
}

TEST_P(QuicPacketCreatorTest, ReserializeFramesWithFullPacketAndPadding) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  const size_t overhead =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      GetEncryptionOverhead() +
      GetStreamFrameOverhead(client_framer_.transport_version());
  size_t capacity = kDefaultMaxPacketSize - overhead;
  for (int delta = -5; delta <= 0; ++delta) {
    std::string data(capacity + delta, 'A');
    size_t bytes_free = 0 - delta;

    QuicFrame frame;
    SimpleDataProducer producer;
    QuicPacketCreatorPeer::framer(&creator_)->set_data_producer(&producer);
    MakeIOVector(data, &iov_);
    QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        client_framer_.transport_version(), Perspective::IS_CLIENT);
    producer.SaveStreamData(stream_id, &iov_, 1u, 0u, iov_.iov_len);
    QuicPacketCreatorPeer::CreateStreamFrame(&creator_, stream_id, iov_.iov_len,
                                             kOffset, false, &frame);
    QuicFrames frames;
    frames.push_back(frame);
    char buffer[kMaxOutgoingPacketSize];
    QuicPendingRetransmission retransmission(CreateRetransmission(
        frames, false /* has_crypto_handshake */, -1 /* needs full padding */,
        ENCRYPTION_FORWARD_SECURE,
        QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    creator_.ReserializeAllFrames(retransmission, buffer,
                                  kMaxOutgoingPacketSize);

    // If there is not enough space in the packet to fit a padding frame
    // (1 byte) and to expand the stream frame (another 2 bytes) the packet
    // will not be padded.
    if (bytes_free < 3) {
      EXPECT_EQ(kDefaultMaxPacketSize - bytes_free,
                serialized_packet_.encrypted_length);
    } else {
      EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.encrypted_length);
    }

    frames_.clear();
  }
}

TEST_P(QuicPacketCreatorTest, SerializeConnectionClose) {
  QuicConnectionCloseFrame frame(QUIC_NO_ERROR, "error");
  if (VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    frame.close_type = IETF_QUIC_TRANSPORT_CONNECTION_CLOSE;
  }

  QuicFrames frames;
  frames.push_back(QuicFrame(&frame));
  SerializedPacket serialized = SerializeAllFrames(frames);
  EXPECT_EQ(ENCRYPTION_INITIAL, serialized.encryption_level);
  ASSERT_EQ(QuicPacketNumber(1u), serialized.packet_number);
  ASSERT_EQ(QuicPacketNumber(1u), creator_.packet_number());

  InSequence s;
  EXPECT_CALL(framer_visitor_, OnPacket());
  EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
  EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
  EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
  EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
  EXPECT_CALL(framer_visitor_, OnConnectionCloseFrame(_));
  EXPECT_CALL(framer_visitor_, OnPacketComplete());

  ProcessPacket(serialized);
}

TEST_P(QuicPacketCreatorTest, ConsumeCryptoData) {
  std::string data = "crypto data";
  QuicFrame frame;
  ASSERT_TRUE(creator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data.length(), 0,
                                         /*needs_full_padding=*/true,
                                         NOT_RETRANSMISSION, &frame));
  EXPECT_EQ(frame.crypto_frame->data_length, data.length());
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, ConsumeData) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u,
                                   false, false, NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  EXPECT_EQ(4u, consumed);
  CheckStreamFrame(frame, stream_id, "test", 0u, false);
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, ConsumeDataFin) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u,
                                   true, false, NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  EXPECT_EQ(4u, consumed);
  CheckStreamFrame(frame, stream_id, "test", 0u, true);
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, ConsumeDataFinOnly) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicFrame frame;
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeData(stream_id, nullptr, 0u, 0u, 0u, 0u, true,
                                   false, NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  EXPECT_EQ(0u, consumed);
  CheckStreamFrame(frame, stream_id, std::string(), 0u, true);
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, CreateAllFreeBytesForStreamFrames) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  const size_t overhead =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      GetEncryptionOverhead();
  for (size_t i = overhead + QuicPacketCreator::MinPlaintextPacketSize(
                                 client_framer_.version());
       i < overhead + 100; ++i) {
    SCOPED_TRACE(i);
    creator_.SetMaxPacketLength(i);
    const bool should_have_room =
        i >
        overhead + GetStreamFrameOverhead(client_framer_.transport_version());
    ASSERT_EQ(should_have_room,
              creator_.HasRoomForStreamFrame(GetNthClientInitiatedStreamId(1),
                                             kOffset, /* data_size=*/0xffff));
    if (should_have_room) {
      QuicFrame frame;
      MakeIOVector("testdata", &iov_);
      EXPECT_CALL(delegate_, OnSerializedPacket(_))
          .WillRepeatedly(Invoke(
              this, &QuicPacketCreatorTest::ClearSerializedPacketForTests));
      ASSERT_TRUE(creator_.ConsumeData(GetNthClientInitiatedStreamId(1), &iov_,
                                       1u, iov_.iov_len, 0u, kOffset, false,
                                       false, NOT_RETRANSMISSION, &frame));
      size_t bytes_consumed = frame.stream_frame.data_length;
      EXPECT_LT(0u, bytes_consumed);
      creator_.Flush();
    }
  }
}

TEST_P(QuicPacketCreatorTest, StreamFrameConsumption) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  // Compute the total overhead for a single frame in packet.
  const size_t overhead =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      GetEncryptionOverhead() +
      GetStreamFrameOverhead(client_framer_.transport_version());
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    std::string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;
    QuicFrame frame;
    MakeIOVector(data, &iov_);
    ASSERT_TRUE(creator_.ConsumeData(GetNthClientInitiatedStreamId(1), &iov_,
                                     1u, iov_.iov_len, 0u, kOffset, false,
                                     false, NOT_RETRANSMISSION, &frame));

    // BytesFree() returns bytes available for the next frame, which will
    // be two bytes smaller since the stream frame would need to be grown.
    EXPECT_EQ(2u, creator_.ExpansionOnNewFrame());
    size_t expected_bytes_free = bytes_free < 3 ? 0 : bytes_free - 2;
    EXPECT_EQ(expected_bytes_free, creator_.BytesFree()) << "delta: " << delta;
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.encrypted_buffer);
    DeleteSerializedPacket();
  }
}

TEST_P(QuicPacketCreatorTest, CryptoStreamFramePacketPadding) {
  // This test serializes crypto payloads slightly larger than a packet, which
  // Causes the multi-packet ClientHello check to fail.
  SetQuicFlag(FLAGS_quic_enforce_single_packet_chlo, false);
  // Compute the total overhead for a single frame in packet.
  size_t overhead =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      GetEncryptionOverhead();
  if (QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    overhead +=
        QuicFramer::GetMinCryptoFrameSize(kOffset, kMaxOutgoingPacketSize);
  } else {
    overhead += GetStreamFrameOverhead(client_framer_.transport_version());
  }
  ASSERT_GT(kMaxOutgoingPacketSize, overhead);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    SCOPED_TRACE(delta);
    std::string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;

    QuicFrame frame;
    MakeIOVector(data, &iov_);
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillRepeatedly(
            Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      ASSERT_TRUE(creator_.ConsumeData(
          QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
          &iov_, 1u, iov_.iov_len, 0u, kOffset, false, true, NOT_RETRANSMISSION,
          &frame));
      size_t bytes_consumed = frame.stream_frame.data_length;
      EXPECT_LT(0u, bytes_consumed);
    } else {
      producer_.SaveCryptoData(ENCRYPTION_INITIAL, kOffset, data);
      ASSERT_TRUE(creator_.ConsumeCryptoData(
          ENCRYPTION_INITIAL, data.length(), kOffset,
          /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));
      size_t bytes_consumed = frame.crypto_frame->data_length;
      EXPECT_LT(0u, bytes_consumed);
    }
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.encrypted_buffer);
    // If there is not enough space in the packet to fit a padding frame
    // (1 byte) and to expand the stream frame (another 2 bytes) the packet
    // will not be padded.
    if (bytes_free < 3 &&
        !QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      EXPECT_EQ(kDefaultMaxPacketSize - bytes_free,
                serialized_packet_.encrypted_length);
    } else {
      EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.encrypted_length);
    }
    DeleteSerializedPacket();
  }
}

TEST_P(QuicPacketCreatorTest, NonCryptoStreamFramePacketNonPadding) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  // Compute the total overhead for a single frame in packet.
  const size_t overhead =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      GetEncryptionOverhead() +
      GetStreamFrameOverhead(client_framer_.transport_version());
  ASSERT_GT(kDefaultMaxPacketSize, overhead);
  size_t capacity = kDefaultMaxPacketSize - overhead;
  // Now, test various sizes around this size.
  for (int delta = -5; delta <= 5; ++delta) {
    std::string data(capacity + delta, 'A');
    size_t bytes_free = delta > 0 ? 0 : 0 - delta;

    QuicFrame frame;
    MakeIOVector(data, &iov_);
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    ASSERT_TRUE(creator_.ConsumeData(GetNthClientInitiatedStreamId(1), &iov_,
                                     1u, iov_.iov_len, 0u, kOffset, false,
                                     false, NOT_RETRANSMISSION, &frame));
    size_t bytes_consumed = frame.stream_frame.data_length;
    EXPECT_LT(0u, bytes_consumed);
    creator_.Flush();
    ASSERT_TRUE(serialized_packet_.encrypted_buffer);
    if (bytes_free > 0) {
      EXPECT_EQ(kDefaultMaxPacketSize - bytes_free,
                serialized_packet_.encrypted_length);
    } else {
      EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.encrypted_length);
    }
    DeleteSerializedPacket();
  }
}

TEST_P(QuicPacketCreatorTest, SerializeVersionNegotiationPacket) {
  QuicFramerPeer::SetPerspective(&client_framer_, Perspective::IS_SERVER);
  ParsedQuicVersionVector versions;
  versions.push_back(test::QuicVersionMax());
  const bool ietf_quic =
      VersionHasIetfInvariantHeader(GetParam().version.transport_version);
  const bool has_length_prefix =
      GetParam().version.HasLengthPrefixedConnectionIds();
  std::unique_ptr<QuicEncryptedPacket> encrypted(
      creator_.SerializeVersionNegotiationPacket(ietf_quic, has_length_prefix,
                                                 versions));

  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnVersionNegotiationPacket(_));
  }
  QuicFramerPeer::SetPerspective(&client_framer_, Perspective::IS_CLIENT);
  client_framer_.ProcessPacket(*encrypted);
}

TEST_P(QuicPacketCreatorTest, SerializeConnectivityProbingPacket) {
  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);

    creator_.set_encryption_level(level);

    OwningSerializedPacketPointer encrypted;
    if (VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
      QuicPathFrameBuffer payload = {
          {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xfe}};
      encrypted =
          creator_.SerializePathChallengeConnectivityProbingPacket(&payload);
    } else {
      encrypted = creator_.SerializeConnectivityProbingPacket();
    }
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      if (VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
        EXPECT_CALL(framer_visitor_, OnPathChallengeFrame(_));
        EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      } else {
        EXPECT_CALL(framer_visitor_, OnPingFrame(_));
        EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      }
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    // QuicFramerPeer::SetPerspective(&client_framer_, Perspective::IS_SERVER);
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest, SerializePathChallengeProbePacket) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);

    creator_.set_encryption_level(level);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathChallengeConnectivityProbingPacket(&payload));
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathChallengeFrame(_));
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    // QuicFramerPeer::SetPerspective(&client_framer_, Perspective::IS_SERVER);
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest, SerializePathResponseProbePacket1PayloadPadded) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicDeque<QuicPathFrameBuffer> payloads;
    payloads.push_back(payload0);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathResponseConnectivityProbingPacket(payloads,
                                                                true));
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_));
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest,
       SerializePathResponseProbePacket1PayloadUnPadded) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicDeque<QuicPathFrameBuffer> payloads;
    payloads.push_back(payload0);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathResponseConnectivityProbingPacket(payloads,
                                                                false));
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest, SerializePathResponseProbePacket2PayloadsPadded) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};
  QuicPathFrameBuffer payload1 = {
      {0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicDeque<QuicPathFrameBuffer> payloads;
    payloads.push_back(payload0);
    payloads.push_back(payload1);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathResponseConnectivityProbingPacket(payloads,
                                                                true));
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_)).Times(2);
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest,
       SerializePathResponseProbePacket2PayloadsUnPadded) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};
  QuicPathFrameBuffer payload1 = {
      {0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicDeque<QuicPathFrameBuffer> payloads;
    payloads.push_back(payload0);
    payloads.push_back(payload1);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathResponseConnectivityProbingPacket(payloads,
                                                                false));
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_)).Times(2);
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest, SerializePathResponseProbePacket3PayloadsPadded) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};
  QuicPathFrameBuffer payload1 = {
      {0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde}};
  QuicPathFrameBuffer payload2 = {
      {0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde, 0xad}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicDeque<QuicPathFrameBuffer> payloads;
    payloads.push_back(payload0);
    payloads.push_back(payload1);
    payloads.push_back(payload2);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathResponseConnectivityProbingPacket(payloads,
                                                                true));
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_)).Times(3);
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest,
       SerializePathResponseProbePacket3PayloadsUnpadded) {
  if (!VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};
  QuicPathFrameBuffer payload1 = {
      {0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde}};
  QuicPathFrameBuffer payload2 = {
      {0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde, 0xad}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicDeque<QuicPathFrameBuffer> payloads;
    payloads.push_back(payload0);
    payloads.push_back(payload1);
    payloads.push_back(payload2);

    OwningSerializedPacketPointer encrypted(
        creator_.SerializePathResponseConnectivityProbingPacket(payloads,
                                                                false));
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnPathResponseFrame(_)).Times(3);
    EXPECT_CALL(framer_visitor_, OnPacketComplete());

    server_framer_.ProcessPacket(QuicEncryptedPacket(
        encrypted->encrypted_buffer, encrypted->encrypted_length));
  }
}

TEST_P(QuicPacketCreatorTest, UpdatePacketSequenceNumberLengthLeastAwaiting) {
  if (VersionHasIetfInvariantHeader(GetParam().version.transport_version) &&
      !GetParam().version.SendsVariableLengthPacketNumberInLongHeader()) {
    EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
              QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
    creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  } else {
    EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
              QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
  }

  QuicPacketCreatorPeer::SetPacketNumber(&creator_, 64);
  creator_.UpdatePacketNumberLength(QuicPacketNumber(2),
                                    10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  QuicPacketCreatorPeer::SetPacketNumber(&creator_, 64 * 256);
  creator_.UpdatePacketNumberLength(QuicPacketNumber(2),
                                    10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_2BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  QuicPacketCreatorPeer::SetPacketNumber(&creator_, 64 * 256 * 256);
  creator_.UpdatePacketNumberLength(QuicPacketNumber(2),
                                    10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  QuicPacketCreatorPeer::SetPacketNumber(&creator_,
                                         UINT64_C(64) * 256 * 256 * 256 * 256);
  creator_.UpdatePacketNumberLength(QuicPacketNumber(2),
                                    10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_6BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
}

TEST_P(QuicPacketCreatorTest, UpdatePacketSequenceNumberLengthCwnd) {
  QuicPacketCreatorPeer::SetPacketNumber(&creator_, 1);
  if (VersionHasIetfInvariantHeader(GetParam().version.transport_version) &&
      !GetParam().version.SendsVariableLengthPacketNumberInLongHeader()) {
    EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
              QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
    creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  } else {
    EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
              QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
  }

  creator_.UpdatePacketNumberLength(QuicPacketNumber(1),
                                    10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(QuicPacketNumber(1),
                                    10000 * 256 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_2BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(QuicPacketNumber(1),
                                    10000 * 256 * 256 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  creator_.UpdatePacketNumberLength(
      QuicPacketNumber(1),
      UINT64_C(1000) * 256 * 256 * 256 * 256 / kDefaultMaxPacketSize);
  EXPECT_EQ(PACKET_6BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
}

TEST_P(QuicPacketCreatorTest, SerializeFrame) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  std::string data("test data");
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    QuicStreamFrame stream_frame(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        /*fin=*/false, 0u, QuicStringPiece());
    frames_.push_back(QuicFrame(stream_frame));
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    frames_.push_back(
        QuicFrame(new QuicCryptoFrame(ENCRYPTION_INITIAL, 0, data.length())));
  }
  SerializedPacket serialized = SerializeAllFrames(frames_);

  QuicPacketHeader header;
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_))
        .WillOnce(DoAll(SaveArg<0>(&header), Return(true)));
    if (QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      EXPECT_CALL(framer_visitor_, OnCryptoFrame(_));
    } else {
      EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    }
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized);
  EXPECT_EQ(GetParam().version_serialization, header.version_flag);
  DeleteFrames(&frames_);
}

TEST_P(QuicPacketCreatorTest, SerializeFrameShortData) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  std::string data("a");
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    QuicStreamFrame stream_frame(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        /*fin=*/false, 0u, QuicStringPiece());
    frames_.push_back(QuicFrame(stream_frame));
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    frames_.push_back(
        QuicFrame(new QuicCryptoFrame(ENCRYPTION_INITIAL, 0, data.length())));
  }
  SerializedPacket serialized = SerializeAllFrames(frames_);

  QuicPacketHeader header;
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_))
        .WillOnce(DoAll(SaveArg<0>(&header), Return(true)));
    if (QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      EXPECT_CALL(framer_visitor_, OnCryptoFrame(_));
    } else {
      EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    }
    if (client_framer_.version().HasHeaderProtection()) {
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    }
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized);
  EXPECT_EQ(GetParam().version_serialization, header.version_flag);
  DeleteFrames(&frames_);
}

TEST_P(QuicPacketCreatorTest, ConsumeDataLargerThanOneStreamFrame) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  // A string larger than fits into a frame.
  QuicFrame frame;
  size_t payload_length = creator_.max_packet_length();
  const std::string too_long_payload(payload_length, 'a');
  MakeIOVector(too_long_payload, &iov_);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u,
                                   true, false, NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  // The entire payload could not be consumed.
  EXPECT_GT(payload_length, consumed);
  creator_.Flush();
  DeleteSerializedPacket();
}

TEST_P(QuicPacketCreatorTest, AddFrameAndFlush) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  const size_t max_plaintext_size =
      client_framer_.GetMaxPlaintextSize(creator_.max_packet_length());
  EXPECT_FALSE(creator_.HasPendingFrames());
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    stream_id =
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version());
  }
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(stream_id));
  EXPECT_EQ(max_plaintext_size -
                GetPacketHeaderSize(
                    client_framer_.transport_version(),
                    creator_.GetDestinationConnectionIdLength(),
                    creator_.GetSourceConnectionIdLength(),
                    QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
                    !kIncludeDiversificationNonce,
                    QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
                    QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_),
                    0, QuicPacketCreatorPeer::GetLengthLength(&creator_)),
            creator_.BytesFree());
  StrictMock<MockDebugDelegate> debug;
  creator_.set_debug_delegate(&debug);

  // Add a variety of frame types and then a padding frame.
  QuicAckFrame ack_frame(InitAckFrame(10u));
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  EXPECT_TRUE(
      creator_.AddSavedFrame(QuicFrame(&ack_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(stream_id));

  QuicFrame frame;
  MakeIOVector("test", &iov_);
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  ASSERT_TRUE(creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u,
                                   false, false, NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  EXPECT_EQ(4u, consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingStreamFramesOfStream(stream_id));

  QuicPaddingFrame padding_frame;
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  EXPECT_TRUE(
      creator_.AddSavedFrame(QuicFrame(padding_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_EQ(0u, creator_.BytesFree());

  // Packet is full. Creator will flush.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  EXPECT_FALSE(
      creator_.AddSavedFrame(QuicFrame(&ack_frame), NOT_RETRANSMISSION));

  // Ensure the packet is successfully created.
  ASSERT_TRUE(serialized_packet_.encrypted_buffer);
  ASSERT_FALSE(serialized_packet_.retransmittable_frames.empty());
  const QuicFrames& retransmittable = serialized_packet_.retransmittable_frames;
  ASSERT_EQ(1u, retransmittable.size());
  EXPECT_EQ(STREAM_FRAME, retransmittable[0].type);
  EXPECT_TRUE(serialized_packet_.has_ack);
  EXPECT_EQ(QuicPacketNumber(10u), serialized_packet_.largest_acked);
  DeleteSerializedPacket();

  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(stream_id));
  EXPECT_EQ(max_plaintext_size -
                GetPacketHeaderSize(
                    client_framer_.transport_version(),
                    creator_.GetDestinationConnectionIdLength(),
                    creator_.GetSourceConnectionIdLength(),
                    QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
                    !kIncludeDiversificationNonce,
                    QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
                    QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_),
                    0, QuicPacketCreatorPeer::GetLengthLength(&creator_)),
            creator_.BytesFree());
}

TEST_P(QuicPacketCreatorTest, SerializeAndSendStreamFrame) {
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  EXPECT_FALSE(creator_.HasPendingFrames());

  MakeIOVector("test", &iov_);
  producer_.SaveStreamData(GetNthClientInitiatedStreamId(0), &iov_, 1u, 0u,
                           iov_.iov_len);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  size_t num_bytes_consumed;
  StrictMock<MockDebugDelegate> debug;
  creator_.set_debug_delegate(&debug);
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  creator_.CreateAndSerializeStreamFrame(
      GetNthClientInitiatedStreamId(0), iov_.iov_len, 0, 0, true,
      NOT_RETRANSMISSION, &num_bytes_consumed);
  EXPECT_EQ(4u, num_bytes_consumed);

  // Ensure the packet is successfully created.
  ASSERT_TRUE(serialized_packet_.encrypted_buffer);
  ASSERT_FALSE(serialized_packet_.retransmittable_frames.empty());
  const QuicFrames& retransmittable = serialized_packet_.retransmittable_frames;
  ASSERT_EQ(1u, retransmittable.size());
  EXPECT_EQ(STREAM_FRAME, retransmittable[0].type);
  DeleteSerializedPacket();

  EXPECT_FALSE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, SerializeStreamFrameWithPadding) {
  // Regression test to check that CreateAndSerializeStreamFrame uses a
  // correctly formatted stream frame header when appending padding.

  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  EXPECT_FALSE(creator_.HasPendingFrames());

  // Send one byte of stream data.
  MakeIOVector("a", &iov_);
  producer_.SaveStreamData(GetNthClientInitiatedStreamId(0), &iov_, 1u, 0u,
                           iov_.iov_len);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  size_t num_bytes_consumed;
  creator_.CreateAndSerializeStreamFrame(
      GetNthClientInitiatedStreamId(0), iov_.iov_len, 0, 0, true,
      NOT_RETRANSMISSION, &num_bytes_consumed);
  EXPECT_EQ(1u, num_bytes_consumed);

  // Check that a packet is created.
  ASSERT_TRUE(serialized_packet_.encrypted_buffer);
  ASSERT_FALSE(serialized_packet_.retransmittable_frames.empty());
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    if (client_framer_.version().HasHeaderProtection()) {
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    }
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, AddUnencryptedStreamDataClosesConnection) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  creator_.set_encryption_level(ENCRYPTION_INITIAL);
  EXPECT_CALL(delegate_, OnUnrecoverableError(_, _));
  QuicStreamFrame stream_frame(GetNthClientInitiatedStreamId(0),
                               /*fin=*/false, 0u, QuicStringPiece());
  EXPECT_QUIC_BUG(
      creator_.AddSavedFrame(QuicFrame(stream_frame), NOT_RETRANSMISSION),
      "Cannot send stream data with level: ENCRYPTION_INITIAL");
}

TEST_P(QuicPacketCreatorTest, SendStreamDataWithEncryptionHandshake) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  creator_.set_encryption_level(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(delegate_, OnUnrecoverableError(_, _));
  QuicStreamFrame stream_frame(GetNthClientInitiatedStreamId(0),
                               /*fin=*/false, 0u, QuicStringPiece());
  EXPECT_QUIC_BUG(
      creator_.AddSavedFrame(QuicFrame(stream_frame), NOT_RETRANSMISSION),
      "Cannot send stream data with level: ENCRYPTION_HANDSHAKE");
}

TEST_P(QuicPacketCreatorTest, ChloTooLarge) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  // This test only matters when the crypto handshake is sent in stream frames.
  // TODO(b/128596274): Re-enable when this check is supported for CRYPTO
  // frames.
  if (QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    return;
  }

  CryptoHandshakeMessage message;
  message.set_tag(kCHLO);
  message.set_minimum_size(kMaxOutgoingPacketSize);
  CryptoFramer framer;
  std::unique_ptr<QuicData> message_data;
  message_data = framer.ConstructHandshakeMessage(message);

  struct iovec iov;
  MakeIOVector(QuicStringPiece(message_data->data(), message_data->length()),
               &iov);
  QuicFrame frame;
  EXPECT_CALL(delegate_, OnUnrecoverableError(QUIC_CRYPTO_CHLO_TOO_LARGE, _));
  EXPECT_QUIC_BUG(creator_.ConsumeData(QuicUtils::GetCryptoStreamId(
                                           client_framer_.transport_version()),
                                       &iov, 1u, iov.iov_len, 0u, 0u, false,
                                       false, NOT_RETRANSMISSION, &frame),
                  "Client hello won't fit in a single packet.");
}

TEST_P(QuicPacketCreatorTest, PendingPadding) {
  EXPECT_EQ(0u, creator_.pending_padding_bytes());
  creator_.AddPendingPadding(kMaxNumRandomPaddingBytes * 10);
  EXPECT_EQ(kMaxNumRandomPaddingBytes * 10, creator_.pending_padding_bytes());

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  // Flush all paddings.
  while (creator_.pending_padding_bytes() > 0) {
    creator_.Flush();
    {
      InSequence s;
      EXPECT_CALL(framer_visitor_, OnPacket());
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
      EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
      EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
      EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    // Packet only contains padding.
    ProcessPacket(serialized_packet_);
  }
  EXPECT_EQ(0u, creator_.pending_padding_bytes());
}

TEST_P(QuicPacketCreatorTest, FullPaddingDoesNotConsumePendingPadding) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  creator_.AddPendingPadding(kMaxNumRandomPaddingBytes);
  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeData(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false,
      /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.Flush();
  EXPECT_EQ(kMaxNumRandomPaddingBytes, creator_.pending_padding_bytes());
}

TEST_P(QuicPacketCreatorTest, SendPendingPaddingInRetransmission) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  QuicStreamFrame stream_frame(stream_id,
                               /*fin=*/false, 0u, QuicStringPiece());
  QuicFrames frames;
  frames.push_back(QuicFrame(stream_frame));
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, true, /*num_padding_bytes=*/0, ENCRYPTION_FORWARD_SECURE,
      QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.AddPendingPadding(kMaxNumRandomPaddingBytes);
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(0u, creator_.pending_padding_bytes());
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, SendPacketAfterFullPaddingRetransmission) {
  // Making sure needs_full_padding gets reset after a full padding
  // retransmission.
  EXPECT_EQ(0u, creator_.pending_padding_bytes());
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicFrame frame;
  std::string data = "fake handshake message data";
  MakeIOVector(data, &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    stream_id =
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version());
  }
  producer_.SaveStreamData(stream_id, &iov_, 1u, 0u, iov_.iov_len);
  QuicPacketCreatorPeer::CreateStreamFrame(&creator_, stream_id, iov_.iov_len,
                                           0u, false, &frame);
  QuicFrames frames;
  frames.push_back(frame);
  char buffer[kMaxOutgoingPacketSize];
  QuicPendingRetransmission retransmission(CreateRetransmission(
      frames, true, /*num_padding_bytes=*/-1, ENCRYPTION_FORWARD_SECURE,
      QuicPacketCreatorPeer::GetPacketNumberLength(&creator_)));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.ReserializeAllFrames(retransmission, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(kDefaultMaxPacketSize, serialized_packet_.encrypted_length);
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    // Full padding.
    EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_);

  creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
                       NOT_RETRANSMISSION, &frame);
  creator_.Flush();
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    // needs_full_padding gets reset.
    EXPECT_CALL(framer_visitor_, OnPaddingFrame(_)).Times(0);
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized_packet_);
  DeleteFrames(&frames);
}

TEST_P(QuicPacketCreatorTest, ConsumeDataAndRandomPadding) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  const QuicByteCount kStreamFramePayloadSize = 100u;
  // Set the packet size be enough for one stream frame with 0 stream offset +
  // 1.
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  size_t length =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      GetEncryptionOverhead() +
      QuicFramer::GetMinStreamFrameSize(
          client_framer_.transport_version(), stream_id, 0,
          /*last_frame_in_packet=*/false, kStreamFramePayloadSize + 1) +
      kStreamFramePayloadSize + 1;
  creator_.SetMaxPacketLength(length);
  creator_.AddPendingPadding(kMaxNumRandomPaddingBytes);
  QuicByteCount pending_padding_bytes = creator_.pending_padding_bytes();
  QuicFrame frame;
  char buf[kStreamFramePayloadSize + 1] = {};
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  // Send stream frame of size kStreamFramePayloadSize.
  MakeIOVector(QuicStringPiece(buf, kStreamFramePayloadSize), &iov_);
  creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
                       NOT_RETRANSMISSION, &frame);
  creator_.Flush();
  // 1 byte padding is sent.
  EXPECT_EQ(pending_padding_bytes - 1, creator_.pending_padding_bytes());
  // Send stream frame of size kStreamFramePayloadSize + 1.
  MakeIOVector(QuicStringPiece(buf, kStreamFramePayloadSize + 1), &iov_);
  creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u,
                       kStreamFramePayloadSize, false, false,
                       NOT_RETRANSMISSION, &frame);
  // No padding is sent.
  creator_.Flush();
  EXPECT_EQ(pending_padding_bytes - 1, creator_.pending_padding_bytes());
  // Flush all paddings.
  while (creator_.pending_padding_bytes() > 0) {
    creator_.Flush();
  }
  EXPECT_EQ(0u, creator_.pending_padding_bytes());
}

TEST_P(QuicPacketCreatorTest, FlushWithExternalBuffer) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  char external_buffer[kMaxOutgoingPacketSize];
  char* expected_buffer = external_buffer;
  EXPECT_CALL(delegate_, GetPacketBuffer()).WillOnce(Return(expected_buffer));

  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeData(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false,
      /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke([expected_buffer](SerializedPacket* serialized_packet) {
        EXPECT_EQ(expected_buffer, serialized_packet->encrypted_buffer);
        ClearSerializedPacket(serialized_packet);
      }));
  creator_.Flush();
}

// Test for error found in
// https://bugs.chromium.org/p/chromium/issues/detail?id=859949 where a gap
// length that crosses an IETF VarInt length boundary would cause a
// failure. While this test is not applicable to versions other than version 99,
// it should still work. Hence, it is not made version-specific.
TEST_P(QuicPacketCreatorTest, IetfAckGapErrorRegression) {
  QuicAckFrame ack_frame =
      InitAckFrame({{QuicPacketNumber(60), QuicPacketNumber(61)},
                    {QuicPacketNumber(125), QuicPacketNumber(126)}});
  frames_.push_back(QuicFrame(&ack_frame));
  SerializeAllFrames(frames_);
}

TEST_P(QuicPacketCreatorTest, AddMessageFrame) {
  if (!VersionSupportsMessageFrames(client_framer_.transport_version())) {
    return;
  }
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .Times(3)
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorTest::ClearSerializedPacketForTests));
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  // Verify that there is enough room for the largest message payload.
  EXPECT_TRUE(creator_.HasRoomForMessageFrame(
      creator_.GetCurrentLargestMessagePayload()));
  std::string message(creator_.GetCurrentLargestMessagePayload(), 'a');
  QuicMessageFrame* message_frame =
      new QuicMessageFrame(1, MakeSpan(&allocator_, message, &storage));
  EXPECT_TRUE(
      creator_.AddSavedFrame(QuicFrame(message_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  creator_.Flush();

  QuicMessageFrame* frame2 =
      new QuicMessageFrame(2, MakeSpan(&allocator_, "message", &storage));
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(frame2), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  // Verify if a new frame is added, 1 byte message length will be added.
  EXPECT_EQ(1u, creator_.ExpansionOnNewFrame());
  QuicMessageFrame* frame3 =
      new QuicMessageFrame(3, MakeSpan(&allocator_, "message2", &storage));
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(frame3), NOT_RETRANSMISSION));
  EXPECT_EQ(1u, creator_.ExpansionOnNewFrame());
  creator_.Flush();

  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  EXPECT_TRUE(creator_.ConsumeData(stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u,
                                   false, false, NOT_RETRANSMISSION, &frame));
  QuicMessageFrame* frame4 =
      new QuicMessageFrame(4, MakeSpan(&allocator_, "message", &storage));
  EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(frame4), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  // Verify there is not enough room for largest payload.
  EXPECT_FALSE(creator_.HasRoomForMessageFrame(
      creator_.GetCurrentLargestMessagePayload()));
  // Add largest message will causes the flush of the stream frame.
  QuicMessageFrame frame5(5, MakeSpan(&allocator_, message, &storage));
  EXPECT_FALSE(creator_.AddSavedFrame(QuicFrame(&frame5), NOT_RETRANSMISSION));
  EXPECT_FALSE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, MessageFrameConsumption) {
  if (!VersionSupportsMessageFrames(client_framer_.transport_version())) {
    return;
  }
  std::string message_data(kDefaultMaxPacketSize, 'a');
  QuicStringPiece message_buffer(message_data);
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  // Test all possible encryption levels of message frames.
  for (EncryptionLevel level :
       {ENCRYPTION_ZERO_RTT, ENCRYPTION_FORWARD_SECURE}) {
    creator_.set_encryption_level(level);
    // Test all possible sizes of message frames.
    for (size_t message_size = 0;
         message_size <= creator_.GetCurrentLargestMessagePayload();
         ++message_size) {
      QuicMessageFrame* frame = new QuicMessageFrame(
          0, MakeSpan(&allocator_,
                      QuicStringPiece(message_buffer.data(), message_size),
                      &storage));
      EXPECT_TRUE(creator_.AddSavedFrame(QuicFrame(frame), NOT_RETRANSMISSION));
      EXPECT_TRUE(creator_.HasPendingFrames());

      size_t expansion_bytes = message_size >= 64 ? 2 : 1;
      EXPECT_EQ(expansion_bytes, creator_.ExpansionOnNewFrame());
      // Verify BytesFree returns bytes available for the next frame, which
      // should subtract the message length.
      size_t expected_bytes_free =
          creator_.GetCurrentLargestMessagePayload() - message_size <
                  expansion_bytes
              ? 0
              : creator_.GetCurrentLargestMessagePayload() - expansion_bytes -
                    message_size;
      EXPECT_EQ(expected_bytes_free, creator_.BytesFree());
      EXPECT_LE(creator_.GetGuaranteedLargestMessagePayload(),
                creator_.GetCurrentLargestMessagePayload());
      EXPECT_CALL(delegate_, OnSerializedPacket(_))
          .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
      creator_.Flush();
      ASSERT_TRUE(serialized_packet_.encrypted_buffer);
      DeleteSerializedPacket();
    }
  }
}

// Regression test for bugfix of GetPacketHeaderSize.
TEST_P(QuicPacketCreatorTest, GetGuaranteedLargestMessagePayload) {
  QuicTransportVersion version = GetParam().version.transport_version;
  if (!VersionSupportsMessageFrames(version)) {
    return;
  }
  QuicPacketLength expected_largest_payload = 1319;
  if (QuicVersionHasLongHeaderLengths(version)) {
    expected_largest_payload -= 2;
  }
  if (GetParam().version.HasLengthPrefixedConnectionIds()) {
    expected_largest_payload -= 1;
  }
  EXPECT_EQ(expected_largest_payload,
            creator_.GetGuaranteedLargestMessagePayload());
}

TEST_P(QuicPacketCreatorTest, PacketTransmissionType) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  creator_.set_can_set_transmission_type(true);
  creator_.SetTransmissionType(NOT_RETRANSMISSION);

  QuicAckFrame temp_ack_frame = InitAckFrame(1);
  QuicFrame ack_frame(&temp_ack_frame);
  ASSERT_FALSE(QuicUtils::IsRetransmittableFrame(ack_frame.type));

  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  QuicFrame stream_frame(QuicStreamFrame(stream_id,
                                         /*fin=*/false, 0u, QuicStringPiece()));
  ASSERT_TRUE(QuicUtils::IsRetransmittableFrame(stream_frame.type));

  QuicFrame padding_frame{QuicPaddingFrame()};
  ASSERT_FALSE(QuicUtils::IsRetransmittableFrame(padding_frame.type));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));

  EXPECT_TRUE(creator_.AddSavedFrame(ack_frame, LOSS_RETRANSMISSION));
  ASSERT_FALSE(serialized_packet_.encrypted_buffer);

  EXPECT_TRUE(creator_.AddSavedFrame(stream_frame, RTO_RETRANSMISSION));
  ASSERT_FALSE(serialized_packet_.encrypted_buffer);

  EXPECT_TRUE(creator_.AddSavedFrame(padding_frame, TLP_RETRANSMISSION));
  creator_.Flush();
  ASSERT_TRUE(serialized_packet_.encrypted_buffer);

  if (creator_.can_set_transmission_type()) {
    // The last retransmittable frame on packet is a stream frame, the packet's
    // transmission type should be the same as the stream frame's.
    EXPECT_EQ(serialized_packet_.transmission_type, RTO_RETRANSMISSION);
  } else {
    EXPECT_EQ(serialized_packet_.transmission_type, NOT_RETRANSMISSION);
  }
  DeleteSerializedPacket();
}

TEST_P(QuicPacketCreatorTest, RetryToken) {
  if (!GetParam().version_serialization ||
      !QuicVersionHasLongHeaderLengths(client_framer_.transport_version())) {
    return;
  }

  char retry_token_bytes[] = {1, 2,  3,  4,  5,  6,  7,  8,
                              9, 10, 11, 12, 13, 14, 15, 16};

  creator_.SetRetryToken(
      std::string(retry_token_bytes, sizeof(retry_token_bytes)));

  std::string data("a");
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    QuicStreamFrame stream_frame(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
        /*fin=*/false, 0u, QuicStringPiece());
    frames_.push_back(QuicFrame(stream_frame));
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, 0, data);
    frames_.push_back(
        QuicFrame(new QuicCryptoFrame(ENCRYPTION_INITIAL, 0, data.length())));
  }
  SerializedPacket serialized = SerializeAllFrames(frames_);

  QuicPacketHeader header;
  {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_))
        .WillOnce(DoAll(SaveArg<0>(&header), Return(true)));
    if (QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
      EXPECT_CALL(framer_visitor_, OnCryptoFrame(_));
    } else {
      EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    }
    if (client_framer_.version().HasHeaderProtection()) {
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    }
    EXPECT_CALL(framer_visitor_, OnPacketComplete());
  }
  ProcessPacket(serialized);
  ASSERT_TRUE(header.version_flag);
  ASSERT_EQ(header.long_packet_type, INITIAL);
  ASSERT_EQ(header.retry_token.length(), sizeof(retry_token_bytes));
  test::CompareCharArraysWithHexError(
      "retry token", header.retry_token.data(), header.retry_token.length(),
      retry_token_bytes, sizeof(retry_token_bytes));
  DeleteFrames(&frames_);
}

TEST_P(QuicPacketCreatorTest, GetConnectionId) {
  EXPECT_EQ(TestConnectionId(2), creator_.GetDestinationConnectionId());
  EXPECT_EQ(EmptyQuicConnectionId(), creator_.GetSourceConnectionId());
}

TEST_P(QuicPacketCreatorTest, ClientConnectionId) {
  if (!client_framer_.version().SupportsClientConnectionIds()) {
    return;
  }
  EXPECT_EQ(TestConnectionId(2), creator_.GetDestinationConnectionId());
  EXPECT_EQ(EmptyQuicConnectionId(), creator_.GetSourceConnectionId());
  creator_.SetClientConnectionId(TestConnectionId(0x33));
  EXPECT_EQ(TestConnectionId(2), creator_.GetDestinationConnectionId());
  EXPECT_EQ(TestConnectionId(0x33), creator_.GetSourceConnectionId());
}

}  // namespace
}  // namespace test
}  // namespace quic
