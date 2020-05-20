// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_framer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_packet_creator_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_data_producer.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_quic_framer.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

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

const QuicPacketNumber kPacketNumber = QuicPacketNumber(UINT64_C(0x12345678));
// Use fields in which each byte is distinct to ensure that every byte is
// framed correctly. The values are otherwise arbitrary.
QuicConnectionId CreateTestConnectionId() {
  return TestConnectionId(UINT64_C(0xFEDCBA9876543210));
}

// Run tests with combinations of {ParsedQuicVersion,
// ToggleVersionSerialization}.
struct TestParams {
  TestParams(ParsedQuicVersion version, bool version_serialization)
      : version(version), version_serialization(version_serialization) {}

  ParsedQuicVersion version;
  bool version_serialization;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  return quiche::QuicheStrCat(ParsedQuicVersionToString(p.version), "_",
                              (p.version_serialization ? "Include" : "No"),
                              "Version");
}

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

  MOCK_METHOD1(OnStreamFrameCoalesced, void(const QuicStreamFrame& frame));
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

  bool ConsumeDataToFillCurrentPacket(QuicStreamId id,
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
    return QuicPacketCreator::ConsumeDataToFillCurrentPacket(
        id, data_length - iov_offset, offset, fin, needs_full_padding,
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
    EXPECT_CALL(delegate_, GetPacketBuffer()).WillRepeatedly(Return(nullptr));
    creator_.SetEncrypter(ENCRYPTION_INITIAL, std::make_unique<NullEncrypter>(
                                                  Perspective::IS_CLIENT));
    creator_.SetEncrypter(ENCRYPTION_HANDSHAKE, std::make_unique<NullEncrypter>(
                                                    Perspective::IS_CLIENT));
    creator_.SetEncrypter(ENCRYPTION_ZERO_RTT, std::make_unique<NullEncrypter>(
                                                   Perspective::IS_CLIENT));
    creator_.SetEncrypter(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullEncrypter>(Perspective::IS_CLIENT));
    client_framer_.set_visitor(&framer_visitor_);
    server_framer_.set_visitor(&framer_visitor_);
    client_framer_.set_data_producer(&producer_);
    if (server_framer_.version().KnowsWhichDecrypterToUse()) {
      server_framer_.InstallDecrypter(
          ENCRYPTION_INITIAL,
          std::make_unique<NullDecrypter>(Perspective::IS_SERVER));
      server_framer_.InstallDecrypter(
          ENCRYPTION_ZERO_RTT,
          std::make_unique<NullDecrypter>(Perspective::IS_SERVER));
      server_framer_.InstallDecrypter(
          ENCRYPTION_HANDSHAKE,
          std::make_unique<NullDecrypter>(Perspective::IS_SERVER));
      server_framer_.InstallDecrypter(
          ENCRYPTION_FORWARD_SECURE,
          std::make_unique<NullDecrypter>(Perspective::IS_SERVER));
    } else {
      server_framer_.SetDecrypter(
          ENCRYPTION_INITIAL,
          std::make_unique<NullDecrypter>(Perspective::IS_SERVER));
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
    QuicDataWriter writer(kMaxOutgoingPacketSize, buf, quiche::HOST_BYTE_ORDER);
    if (frame.stream_frame.data_length > 0) {
      producer_.WriteStreamData(stream_id, frame.stream_frame.offset,
                                frame.stream_frame.data_length, &writer);
    }
    EXPECT_EQ(data,
              quiche::QuicheStringPiece(buf, frame.stream_frame.data_length));
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
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicPacketCreatorTest, SerializeFrames) {
  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);
    frames_.push_back(QuicFrame(new QuicAckFrame(InitAckFrame(1))));
    QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
        client_framer_.transport_version(), Perspective::IS_CLIENT);
    if (level != ENCRYPTION_INITIAL && level != ENCRYPTION_HANDSHAKE) {
      frames_.push_back(QuicFrame(
          QuicStreamFrame(stream_id, false, 0u, quiche::QuicheStringPiece())));
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
      }
      if (client_framer_.version().HasHeaderProtection()) {
        EXPECT_CALL(framer_visitor_, OnPaddingFrame(_))
            .Times(testing::AnyNumber());
      }
      EXPECT_CALL(framer_visitor_, OnPacketComplete());
    }
    ProcessPacket(serialized);
  }
}

TEST_P(QuicPacketCreatorTest, SerializeConnectionClose) {
  QuicConnectionCloseFrame frame(creator_.transport_version(), QUIC_NO_ERROR,
                                 "error",
                                 /*transport_close_frame_type=*/0);

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

TEST_P(QuicPacketCreatorTest, ConsumeCryptoDataToFillCurrentPacket) {
  std::string data = "crypto data";
  QuicFrame frame;
  ASSERT_TRUE(creator_.ConsumeCryptoDataToFillCurrentPacket(
      ENCRYPTION_INITIAL, data.length(), 0,
      /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));
  EXPECT_EQ(frame.crypto_frame->data_length, data.length());
  EXPECT_TRUE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, ConsumeDataToFillCurrentPacket) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
      NOT_RETRANSMISSION, &frame));
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
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, true, false,
      NOT_RETRANSMISSION, &frame));
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
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, nullptr, 0u, 0u, 0u, 0u, true, false, NOT_RETRANSMISSION,
      &frame));
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
      ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
          GetNthClientInitiatedStreamId(1), &iov_, 1u, iov_.iov_len, 0u,
          kOffset, false, false, NOT_RETRANSMISSION, &frame));
      size_t bytes_consumed = frame.stream_frame.data_length;
      EXPECT_LT(0u, bytes_consumed);
      creator_.FlushCurrentPacket();
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
    ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
        GetNthClientInitiatedStreamId(1), &iov_, 1u, iov_.iov_len, 0u, kOffset,
        false, false, NOT_RETRANSMISSION, &frame));

    // BytesFree() returns bytes available for the next frame, which will
    // be two bytes smaller since the stream frame would need to be grown.
    EXPECT_EQ(2u, creator_.ExpansionOnNewFrame());
    size_t expected_bytes_free = bytes_free < 3 ? 0 : bytes_free - 2;
    EXPECT_EQ(expected_bytes_free, creator_.BytesFree()) << "delta: " << delta;
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
    creator_.FlushCurrentPacket();
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
      ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
          QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
          &iov_, 1u, iov_.iov_len, 0u, kOffset, false, true, NOT_RETRANSMISSION,
          &frame));
      size_t bytes_consumed = frame.stream_frame.data_length;
      EXPECT_LT(0u, bytes_consumed);
    } else {
      producer_.SaveCryptoData(ENCRYPTION_INITIAL, kOffset, data);
      ASSERT_TRUE(creator_.ConsumeCryptoDataToFillCurrentPacket(
          ENCRYPTION_INITIAL, data.length(), kOffset,
          /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));
      size_t bytes_consumed = frame.crypto_frame->data_length;
      EXPECT_LT(0u, bytes_consumed);
    }
    creator_.FlushCurrentPacket();
    ASSERT_TRUE(serialized_packet_.encrypted_buffer);
    // If there is not enough space in the packet to fit a padding frame
    // (1 byte) and to expand the stream frame (another 2 bytes) the packet
    // will not be padded.
    // Padding is skipped when we try to send coalesced packets.
    if ((bytes_free < 3 &&
         !QuicVersionUsesCryptoFrames(client_framer_.transport_version())) ||
        client_framer_.version().CanSendCoalescedPackets()) {
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
    ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
        GetNthClientInitiatedStreamId(1), &iov_, 1u, iov_.iov_len, 0u, kOffset,
        false, false, NOT_RETRANSMISSION, &frame));
    size_t bytes_consumed = frame.stream_frame.data_length;
    EXPECT_LT(0u, bytes_consumed);
    creator_.FlushCurrentPacket();
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
      VersionHasIetfInvariantHeader(creator_.transport_version());
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

// Test that the path challenge connectivity probing packet is serialized
// correctly as a padded PATH CHALLENGE packet.
TEST_P(QuicPacketCreatorTest, BuildPathChallengePacket) {
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    // This frame is only for IETF QUIC.
    return;
  }

  QuicPacketHeader header;
  header.destination_connection_id = CreateTestConnectionId();
  header.reset_flag = false;
  header.version_flag = false;
  header.packet_number = kPacketNumber;
  QuicPathFrameBuffer payload;

  // clang-format off
  unsigned char packet[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // Path Challenge Frame type (IETF_PATH_CHALLENGE)
    0x1a,
    // 8 "random" bytes, MockRandom makes lots of r's
    'r', 'r', 'r', 'r', 'r', 'r', 'r', 'r',
    // frame type (padding frame)
    0x00,
    0x00, 0x00, 0x00, 0x00
  };
  // clang-format on

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  MockRandom randomizer;

  size_t length = creator_.BuildPaddedPathChallengePacket(
      header, buffer.get(), QUICHE_ARRAYSIZE(packet), &payload, &randomizer,
      ENCRYPTION_INITIAL);
  EXPECT_EQ(length, QUICHE_ARRAYSIZE(packet));

  // Payload has the random bytes that were generated. Copy them into packet,
  // above, before checking that the generated packet is correct.
  EXPECT_EQ(kQuicPathFrameBufferSize, payload.size());

  QuicPacket data(creator_.transport_version(), buffer.release(), length, true,
                  header);

  quiche::test::CompareCharArraysWithHexError(
      "constructed packet", data.data(), data.length(),
      reinterpret_cast<char*>(packet), QUICHE_ARRAYSIZE(packet));
}

TEST_P(QuicPacketCreatorTest, BuildConnectivityProbingPacket) {
  QuicPacketHeader header;
  header.destination_connection_id = CreateTestConnectionId();
  header.reset_flag = false;
  header.version_flag = false;
  header.packet_number = kPacketNumber;

  // clang-format off
  unsigned char packet[] = {
    // public flags (8 byte connection_id)
    0x2C,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // frame type (ping frame)
    0x07,
    // frame type (padding frame)
    0x00,
    0x00, 0x00, 0x00, 0x00
  };

  unsigned char packet46[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // frame type
    0x07,
    // frame type (padding frame)
    0x00,
    0x00, 0x00, 0x00, 0x00
  };

  unsigned char packet99[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // frame type (IETF_PING frame)
    0x01,
    // frame type (padding frame)
    0x00,
    0x00, 0x00, 0x00, 0x00
  };
  // clang-format on

  unsigned char* p = packet;
  size_t packet_size = QUICHE_ARRAYSIZE(packet);
  if (VersionHasIetfQuicFrames(creator_.transport_version())) {
    p = packet99;
    packet_size = QUICHE_ARRAYSIZE(packet99);
  } else if (creator_.transport_version() >= QUIC_VERSION_46) {
    p = packet46;
    packet_size = QUICHE_ARRAYSIZE(packet46);
  }

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);

  size_t length = creator_.BuildConnectivityProbingPacket(
      header, buffer.get(), packet_size, ENCRYPTION_INITIAL);

  EXPECT_NE(0u, length);
  QuicPacket data(creator_.transport_version(), buffer.release(), length, true,
                  header);

  quiche::test::CompareCharArraysWithHexError(
      "constructed packet", data.data(), data.length(),
      reinterpret_cast<char*>(p), packet_size);
}

// Several tests that the path response connectivity probing packet is
// serialized correctly as either a padded and unpadded PATH RESPONSE
// packet. Also generates packets with 1 and 3 PATH_RESPONSES in them to
// exercised the single- and multiple- payload cases.
TEST_P(QuicPacketCreatorTest, BuildPathResponsePacket1ResponseUnpadded) {
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    // This frame is only for IETF QUIC.
    return;
  }

  QuicPacketHeader header;
  header.destination_connection_id = CreateTestConnectionId();
  header.reset_flag = false;
  header.version_flag = false;
  header.packet_number = kPacketNumber;
  QuicPathFrameBuffer payload0 = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};

  // Build 1 PATH RESPONSE, not padded
  // clang-format off
  unsigned char packet[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // Path Response Frame type (IETF_PATH_RESPONSE)
    0x1b,
    // 8 "random" bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  };
  // clang-format on
  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  QuicCircularDeque<QuicPathFrameBuffer> payloads;
  payloads.push_back(payload0);
  size_t length = creator_.BuildPathResponsePacket(
      header, buffer.get(), QUICHE_ARRAYSIZE(packet), payloads,
      /*is_padded=*/false, ENCRYPTION_INITIAL);
  EXPECT_EQ(length, QUICHE_ARRAYSIZE(packet));
  QuicPacket data(creator_.transport_version(), buffer.release(), length, true,
                  header);

  quiche::test::CompareCharArraysWithHexError(
      "constructed packet", data.data(), data.length(),
      reinterpret_cast<char*>(packet), QUICHE_ARRAYSIZE(packet));
}

TEST_P(QuicPacketCreatorTest, BuildPathResponsePacket1ResponsePadded) {
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    // This frame is only for IETF QUIC.
    return;
  }

  QuicPacketHeader header;
  header.destination_connection_id = CreateTestConnectionId();
  header.reset_flag = false;
  header.version_flag = false;
  header.packet_number = kPacketNumber;
  QuicPathFrameBuffer payload0 = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};

  // Build 1 PATH RESPONSE, padded
  // clang-format off
  unsigned char packet[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // Path Response Frame type (IETF_PATH_RESPONSE)
    0x1b,
    // 8 "random" bytes
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    // Padding type and pad
    0x00, 0x00, 0x00, 0x00, 0x00
  };
  // clang-format on
  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  QuicCircularDeque<QuicPathFrameBuffer> payloads;
  payloads.push_back(payload0);
  size_t length = creator_.BuildPathResponsePacket(
      header, buffer.get(), QUICHE_ARRAYSIZE(packet), payloads,
      /*is_padded=*/true, ENCRYPTION_INITIAL);
  EXPECT_EQ(length, QUICHE_ARRAYSIZE(packet));
  QuicPacket data(creator_.transport_version(), buffer.release(), length, true,
                  header);

  quiche::test::CompareCharArraysWithHexError(
      "constructed packet", data.data(), data.length(),
      reinterpret_cast<char*>(packet), QUICHE_ARRAYSIZE(packet));
}

TEST_P(QuicPacketCreatorTest, BuildPathResponsePacket3ResponsesUnpadded) {
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    // This frame is only for IETF QUIC.
    return;
  }

  QuicPacketHeader header;
  header.destination_connection_id = CreateTestConnectionId();
  header.reset_flag = false;
  header.version_flag = false;
  header.packet_number = kPacketNumber;
  QuicPathFrameBuffer payload0 = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};
  QuicPathFrameBuffer payload1 = {
      {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18}};
  QuicPathFrameBuffer payload2 = {
      {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28}};

  // Build one packet with 3 PATH RESPONSES, no padding
  // clang-format off
  unsigned char packet[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // 3 path response frames (IETF_PATH_RESPONSE type byte and payload)
    0x1b, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x1b, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x1b, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
  };
  // clang-format on

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  QuicCircularDeque<QuicPathFrameBuffer> payloads;
  payloads.push_back(payload0);
  payloads.push_back(payload1);
  payloads.push_back(payload2);
  size_t length = creator_.BuildPathResponsePacket(
      header, buffer.get(), QUICHE_ARRAYSIZE(packet), payloads,
      /*is_padded=*/false, ENCRYPTION_INITIAL);
  EXPECT_EQ(length, QUICHE_ARRAYSIZE(packet));
  QuicPacket data(creator_.transport_version(), buffer.release(), length, true,
                  header);

  quiche::test::CompareCharArraysWithHexError(
      "constructed packet", data.data(), data.length(),
      reinterpret_cast<char*>(packet), QUICHE_ARRAYSIZE(packet));
}

TEST_P(QuicPacketCreatorTest, BuildPathResponsePacket3ResponsesPadded) {
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    // This frame is only for IETF QUIC.
    return;
  }

  QuicPacketHeader header;
  header.destination_connection_id = CreateTestConnectionId();
  header.reset_flag = false;
  header.version_flag = false;
  header.packet_number = kPacketNumber;
  QuicPathFrameBuffer payload0 = {
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};
  QuicPathFrameBuffer payload1 = {
      {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18}};
  QuicPathFrameBuffer payload2 = {
      {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28}};

  // Build one packet with 3 PATH RESPONSES, with padding
  // clang-format off
  unsigned char packet[] = {
    // type (short header, 4 byte packet number)
    0x43,
    // connection_id
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    // packet number
    0x12, 0x34, 0x56, 0x78,

    // 3 path response frames (IETF_PATH_RESPONSE byte and payload)
    0x1b, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x1b, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x1b, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    // Padding
    0x00, 0x00, 0x00, 0x00, 0x00
  };
  // clang-format on

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  QuicCircularDeque<QuicPathFrameBuffer> payloads;
  payloads.push_back(payload0);
  payloads.push_back(payload1);
  payloads.push_back(payload2);
  size_t length = creator_.BuildPathResponsePacket(
      header, buffer.get(), QUICHE_ARRAYSIZE(packet), payloads,
      /*is_padded=*/true, ENCRYPTION_INITIAL);
  EXPECT_EQ(length, QUICHE_ARRAYSIZE(packet));
  QuicPacket data(creator_.transport_version(), buffer.release(), length, true,
                  header);

  quiche::test::CompareCharArraysWithHexError(
      "constructed packet", data.data(), data.length(),
      reinterpret_cast<char*>(packet), QUICHE_ARRAYSIZE(packet));
}

TEST_P(QuicPacketCreatorTest, SerializeConnectivityProbingPacket) {
  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);

    creator_.set_encryption_level(level);

    OwningSerializedPacketPointer encrypted;
    if (VersionHasIetfQuicFrames(creator_.transport_version())) {
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
      if (VersionHasIetfQuicFrames(creator_.transport_version())) {
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicCircularDeque<QuicPathFrameBuffer> payloads;
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicCircularDeque<QuicPathFrameBuffer> payloads;
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};
  QuicPathFrameBuffer payload1 = {
      {0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicCircularDeque<QuicPathFrameBuffer> payloads;
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
    return;
  }
  QuicPathFrameBuffer payload0 = {
      {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee}};
  QuicPathFrameBuffer payload1 = {
      {0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xee, 0xde}};

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);

    QuicCircularDeque<QuicPathFrameBuffer> payloads;
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
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

    QuicCircularDeque<QuicPathFrameBuffer> payloads;
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
  if (!VersionHasIetfQuicFrames(creator_.transport_version())) {
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

    QuicCircularDeque<QuicPathFrameBuffer> payloads;
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
  if (VersionHasIetfInvariantHeader(creator_.transport_version()) &&
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
  if (VersionHasIetfInvariantHeader(creator_.transport_version()) &&
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

TEST_P(QuicPacketCreatorTest, SkipNPacketNumbers) {
  QuicPacketCreatorPeer::SetPacketNumber(&creator_, 1);
  if (VersionHasIetfInvariantHeader(creator_.transport_version()) &&
      !GetParam().version.SendsVariableLengthPacketNumberInLongHeader()) {
    EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
              QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
    creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  } else {
    EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
              QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));
  }
  creator_.SkipNPacketNumbers(63, QuicPacketNumber(2),
                              10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(QuicPacketNumber(64), creator_.packet_number());
  EXPECT_EQ(PACKET_1BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  creator_.SkipNPacketNumbers(64 * 255, QuicPacketNumber(2),
                              10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(QuicPacketNumber(64 * 256), creator_.packet_number());
  EXPECT_EQ(PACKET_2BYTE_PACKET_NUMBER,
            QuicPacketCreatorPeer::GetPacketNumberLength(&creator_));

  creator_.SkipNPacketNumbers(64 * 256 * 255, QuicPacketNumber(2),
                              10000 / kDefaultMaxPacketSize);
  EXPECT_EQ(QuicPacketNumber(64 * 256 * 256), creator_.packet_number());
  EXPECT_EQ(PACKET_4BYTE_PACKET_NUMBER,
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
        /*fin=*/false, 0u, quiche::QuicheStringPiece());
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
        /*fin=*/false, 0u, quiche::QuicheStringPiece());
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
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, true, false,
      NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  // The entire payload could not be consumed.
  EXPECT_GT(payload_length, consumed);
  creator_.FlushCurrentPacket();
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
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(&ack_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(stream_id));

  QuicFrame frame;
  MakeIOVector("test", &iov_);
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
      NOT_RETRANSMISSION, &frame));
  size_t consumed = frame.stream_frame.data_length;
  EXPECT_EQ(4u, consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingStreamFramesOfStream(stream_id));

  QuicPaddingFrame padding_frame;
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(padding_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_EQ(0u, creator_.BytesFree());

  // Packet is full. Creator will flush.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  EXPECT_FALSE(creator_.AddFrame(QuicFrame(&ack_frame), NOT_RETRANSMISSION));

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
                               /*fin=*/false, 0u, quiche::QuicheStringPiece());
  EXPECT_QUIC_BUG(
      creator_.AddFrame(QuicFrame(stream_frame), NOT_RETRANSMISSION),
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
                               /*fin=*/false, 0u, quiche::QuicheStringPiece());
  EXPECT_QUIC_BUG(
      creator_.AddFrame(QuicFrame(stream_frame), NOT_RETRANSMISSION),
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
  MakeIOVector(
      quiche::QuicheStringPiece(message_data->data(), message_data->length()),
      &iov);
  QuicFrame frame;
  EXPECT_CALL(delegate_, OnUnrecoverableError(QUIC_CRYPTO_CHLO_TOO_LARGE, _));
  EXPECT_QUIC_BUG(
      creator_.ConsumeDataToFillCurrentPacket(
          QuicUtils::GetCryptoStreamId(client_framer_.transport_version()),
          &iov, 1u, iov.iov_len, 0u, 0u, false, false, NOT_RETRANSMISSION,
          &frame),
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
    creator_.FlushCurrentPacket();
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
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false,
      /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.FlushCurrentPacket();
  EXPECT_EQ(kMaxNumRandomPaddingBytes, creator_.pending_padding_bytes());
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
  MakeIOVector(quiche::QuicheStringPiece(buf, kStreamFramePayloadSize), &iov_);
  creator_.ConsumeDataToFillCurrentPacket(stream_id, &iov_, 1u, iov_.iov_len,
                                          0u, 0u, false, false,
                                          NOT_RETRANSMISSION, &frame);
  creator_.FlushCurrentPacket();
  // 1 byte padding is sent.
  EXPECT_EQ(pending_padding_bytes - 1, creator_.pending_padding_bytes());
  // Send stream frame of size kStreamFramePayloadSize + 1.
  MakeIOVector(quiche::QuicheStringPiece(buf, kStreamFramePayloadSize + 1),
               &iov_);
  creator_.ConsumeDataToFillCurrentPacket(stream_id, &iov_, 1u, iov_.iov_len,
                                          0u, kStreamFramePayloadSize, false,
                                          false, NOT_RETRANSMISSION, &frame);
  // No padding is sent.
  creator_.FlushCurrentPacket();
  EXPECT_EQ(pending_padding_bytes - 1, creator_.pending_padding_bytes());
  // Flush all paddings.
  while (creator_.pending_padding_bytes() > 0) {
    creator_.FlushCurrentPacket();
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
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false,
      /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke([expected_buffer](SerializedPacket* serialized_packet) {
        EXPECT_EQ(expected_buffer, serialized_packet->encrypted_buffer);
        ClearSerializedPacket(serialized_packet);
      }));
  creator_.FlushCurrentPacket();
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
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(message_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  creator_.FlushCurrentPacket();

  QuicMessageFrame* frame2 =
      new QuicMessageFrame(2, MakeSpan(&allocator_, "message", &storage));
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(frame2), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  // Verify if a new frame is added, 1 byte message length will be added.
  EXPECT_EQ(1u, creator_.ExpansionOnNewFrame());
  QuicMessageFrame* frame3 =
      new QuicMessageFrame(3, MakeSpan(&allocator_, "message2", &storage));
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(frame3), NOT_RETRANSMISSION));
  EXPECT_EQ(1u, creator_.ExpansionOnNewFrame());
  creator_.FlushCurrentPacket();

  QuicFrame frame;
  MakeIOVector("test", &iov_);
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  EXPECT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
      NOT_RETRANSMISSION, &frame));
  QuicMessageFrame* frame4 =
      new QuicMessageFrame(4, MakeSpan(&allocator_, "message", &storage));
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(frame4), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
  // Verify there is not enough room for largest payload.
  EXPECT_FALSE(creator_.HasRoomForMessageFrame(
      creator_.GetCurrentLargestMessagePayload()));
  // Add largest message will causes the flush of the stream frame.
  QuicMessageFrame frame5(5, MakeSpan(&allocator_, message, &storage));
  EXPECT_FALSE(creator_.AddFrame(QuicFrame(&frame5), NOT_RETRANSMISSION));
  EXPECT_FALSE(creator_.HasPendingFrames());
}

TEST_P(QuicPacketCreatorTest, MessageFrameConsumption) {
  if (!VersionSupportsMessageFrames(client_framer_.transport_version())) {
    return;
  }
  std::string message_data(kDefaultMaxPacketSize, 'a');
  quiche::QuicheStringPiece message_buffer(message_data);
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
          0, MakeSpan(
                 &allocator_,
                 quiche::QuicheStringPiece(message_buffer.data(), message_size),
                 &storage));
      EXPECT_TRUE(creator_.AddFrame(QuicFrame(frame), NOT_RETRANSMISSION));
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
      creator_.FlushCurrentPacket();
      ASSERT_TRUE(serialized_packet_.encrypted_buffer);
      DeleteSerializedPacket();
    }
  }
}

// Regression test for bugfix of GetPacketHeaderSize.
TEST_P(QuicPacketCreatorTest, GetGuaranteedLargestMessagePayload) {
  QuicTransportVersion version = creator_.transport_version();
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

  QuicAckFrame temp_ack_frame = InitAckFrame(1);
  QuicFrame ack_frame(&temp_ack_frame);
  ASSERT_FALSE(QuicUtils::IsRetransmittableFrame(ack_frame.type));

  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  QuicFrame stream_frame(QuicStreamFrame(stream_id,
                                         /*fin=*/false, 0u,
                                         quiche::QuicheStringPiece()));
  ASSERT_TRUE(QuicUtils::IsRetransmittableFrame(stream_frame.type));

  QuicFrame padding_frame{QuicPaddingFrame()};
  ASSERT_FALSE(QuicUtils::IsRetransmittableFrame(padding_frame.type));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));

  EXPECT_TRUE(creator_.AddFrame(ack_frame, LOSS_RETRANSMISSION));
  ASSERT_FALSE(serialized_packet_.encrypted_buffer);

  EXPECT_TRUE(creator_.AddFrame(stream_frame, RTO_RETRANSMISSION));
  ASSERT_FALSE(serialized_packet_.encrypted_buffer);

  EXPECT_TRUE(creator_.AddFrame(padding_frame, TLP_RETRANSMISSION));
  creator_.FlushCurrentPacket();
  ASSERT_TRUE(serialized_packet_.encrypted_buffer);

  // The last retransmittable frame on packet is a stream frame, the packet's
  // transmission type should be the same as the stream frame's.
  EXPECT_EQ(serialized_packet_.transmission_type, RTO_RETRANSMISSION);
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
        /*fin=*/false, 0u, quiche::QuicheStringPiece());
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
  quiche::test::CompareCharArraysWithHexError(
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

TEST_P(QuicPacketCreatorTest, CoalesceStreamFrames) {
  InSequence s;
  if (!GetParam().version_serialization) {
    creator_.StopSendingVersion();
  }
  const size_t max_plaintext_size =
      client_framer_.GetMaxPlaintextSize(creator_.max_packet_length());
  EXPECT_FALSE(creator_.HasPendingFrames());
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicStreamId stream_id1 = QuicUtils::GetFirstBidirectionalStreamId(
      client_framer_.transport_version(), Perspective::IS_CLIENT);
  QuicStreamId stream_id2 = GetNthClientInitiatedStreamId(1);
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(stream_id1));
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

  MakeIOVector("test", &iov_);
  QuicFrame frame;
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id1, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
      NOT_RETRANSMISSION, &frame));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingStreamFramesOfStream(stream_id1));

  MakeIOVector("coalesce", &iov_);
  // frame will be coalesced with the first frame.
  const auto previous_size = creator_.PacketSize();
  QuicStreamFrame target(stream_id1, true, 0, 12);
  EXPECT_CALL(debug, OnStreamFrameCoalesced(target));
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id1, &iov_, 1u, iov_.iov_len, 0u, 4u, true, false,
      NOT_RETRANSMISSION, &frame));
  EXPECT_EQ(frame.stream_frame.data_length,
            creator_.PacketSize() - previous_size);

  // frame is for another stream, so it won't be coalesced.
  const auto length = creator_.BytesFree() - 10u;
  std::string large_data(length, 'x');
  MakeIOVector(large_data, &iov_);
  EXPECT_CALL(debug, OnFrameAddedToPacket(_));
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id2, &iov_, 1u, iov_.iov_len, 0u, 0u, false, false,
      NOT_RETRANSMISSION, &frame));
  EXPECT_TRUE(creator_.HasPendingStreamFramesOfStream(stream_id2));

  // The packet doesn't have enough free bytes for all data, but will still be
  // able to consume and coalesce part of them.
  EXPECT_CALL(debug, OnStreamFrameCoalesced(_));
  MakeIOVector("somerandomdata", &iov_);
  ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
      stream_id2, &iov_, 1u, iov_.iov_len, 0u, length, false, false,
      NOT_RETRANSMISSION, &frame));

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.FlushCurrentPacket();
  EXPECT_CALL(framer_visitor_, OnPacket());
  EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
  EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
  EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
  EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
  // The packet should only have 2 stream frames.
  EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
  EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
  EXPECT_CALL(framer_visitor_, OnPacketComplete());
  ProcessPacket(serialized_packet_);
}

TEST_P(QuicPacketCreatorTest, SaveNonRetransmittableFrames) {
  QuicAckFrame ack_frame(InitAckFrame(1));
  frames_.push_back(QuicFrame(&ack_frame));
  frames_.push_back(QuicFrame(QuicPaddingFrame(-1)));
  SerializedPacket serialized = SerializeAllFrames(frames_);
  ASSERT_EQ(2u, serialized.nonretransmittable_frames.size());
  EXPECT_EQ(ACK_FRAME, serialized.nonretransmittable_frames[0].type);
  EXPECT_EQ(PADDING_FRAME, serialized.nonretransmittable_frames[1].type);
  // Verify full padding frame is translated to a padding frame with actual
  // bytes of padding.
  EXPECT_LT(
      0,
      serialized.nonretransmittable_frames[1].padding_frame.num_padding_bytes);
  frames_.clear();

  // Serialize another packet with the same frames.
  SerializedPacket packet = QuicPacketCreatorPeer::SerializeAllFrames(
      &creator_, serialized.nonretransmittable_frames, buffer_,
      kMaxOutgoingPacketSize);
  // Verify the packet length of both packets are equal.
  EXPECT_EQ(serialized.encrypted_length, packet.encrypted_length);
}

TEST_P(QuicPacketCreatorTest, SerializeCoalescedPacket) {
  QuicCoalescedPacket coalesced;
  SimpleBufferAllocator allocator;
  QuicSocketAddress self_address(QuicIpAddress::Loopback4(), 1);
  QuicSocketAddress peer_address(QuicIpAddress::Loopback4(), 2);
  for (size_t i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    EncryptionLevel level = static_cast<EncryptionLevel>(i);
    creator_.set_encryption_level(level);
    QuicAckFrame ack_frame(InitAckFrame(1));
    frames_.push_back(QuicFrame(&ack_frame));
    if (level != ENCRYPTION_INITIAL && level != ENCRYPTION_HANDSHAKE) {
      frames_.push_back(QuicFrame(
          QuicStreamFrame(1, false, 0u, quiche::QuicheStringPiece())));
    }
    SerializedPacket serialized = SerializeAllFrames(frames_);
    EXPECT_EQ(level, serialized.encryption_level);
    frames_.clear();
    ASSERT_TRUE(coalesced.MaybeCoalescePacket(serialized, self_address,
                                              peer_address, &allocator,
                                              creator_.max_packet_length()));
  }
  char buffer[kMaxOutgoingPacketSize];
  size_t coalesced_length = creator_.SerializeCoalescedPacket(
      coalesced, buffer, kMaxOutgoingPacketSize);
  // Verify packet is padded to full.
  ASSERT_EQ(coalesced.max_packet_length(), coalesced_length);
  if (!QuicVersionHasLongHeaderLengths(server_framer_.transport_version())) {
    return;
  }
  // Verify packet process.
  std::unique_ptr<QuicEncryptedPacket> packets[NUM_ENCRYPTION_LEVELS];
  packets[ENCRYPTION_INITIAL] =
      std::make_unique<QuicEncryptedPacket>(buffer, coalesced_length);
  for (size_t i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; ++i) {
    InSequence s;
    EXPECT_CALL(framer_visitor_, OnPacket());
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedPublicHeader(_));
    if (i < ENCRYPTION_FORWARD_SECURE) {
      // Save coalesced packet.
      EXPECT_CALL(framer_visitor_, OnCoalescedPacket(_))
          .WillOnce(Invoke([i, &packets](const QuicEncryptedPacket& packet) {
            packets[i + 1] = packet.Clone();
          }));
    }
    EXPECT_CALL(framer_visitor_, OnUnauthenticatedHeader(_));
    EXPECT_CALL(framer_visitor_, OnDecryptedPacket(_));
    EXPECT_CALL(framer_visitor_, OnPacketHeader(_));
    EXPECT_CALL(framer_visitor_, OnAckFrameStart(_, _)).WillOnce(Return(true));
    EXPECT_CALL(framer_visitor_,
                OnAckRange(QuicPacketNumber(1), QuicPacketNumber(2)))
        .WillOnce(Return(true));
    EXPECT_CALL(framer_visitor_, OnAckFrameEnd(_)).WillOnce(Return(true));
    if (i == ENCRYPTION_INITIAL) {
      // Verify padding is added.
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_));
    } else {
      EXPECT_CALL(framer_visitor_, OnPaddingFrame(_)).Times(testing::AtMost(1));
    }
    if (i != ENCRYPTION_INITIAL && i != ENCRYPTION_HANDSHAKE) {
      EXPECT_CALL(framer_visitor_, OnStreamFrame(_));
    }
    EXPECT_CALL(framer_visitor_, OnPacketComplete());

    server_framer_.ProcessPacket(*packets[i]);
  }
}

TEST_P(QuicPacketCreatorTest, SoftMaxPacketLength) {
  creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
  QuicByteCount previous_max_packet_length = creator_.max_packet_length();
  const size_t overhead =
      GetPacketHeaderOverhead(client_framer_.transport_version()) +
      QuicPacketCreator::MinPlaintextPacketSize(client_framer_.version()) +
      GetEncryptionOverhead();
  // Make sure a length which cannot accommodate header (includes header
  // protection minimal length) gets rejected.
  creator_.SetSoftMaxPacketLength(overhead - 1);
  EXPECT_EQ(previous_max_packet_length, creator_.max_packet_length());

  creator_.SetSoftMaxPacketLength(overhead);
  EXPECT_EQ(overhead, creator_.max_packet_length());

  // Verify creator has room for stream frame because max_packet_length_ gets
  // restored.
  ASSERT_TRUE(creator_.HasRoomForStreamFrame(
      GetNthClientInitiatedStreamId(1), kMaxIetfVarInt,
      std::numeric_limits<uint32_t>::max()));
  EXPECT_EQ(previous_max_packet_length, creator_.max_packet_length());

  // Same for message frame.
  if (VersionSupportsMessageFrames(client_framer_.transport_version())) {
    creator_.SetSoftMaxPacketLength(overhead);
    // Verify GetCurrentLargestMessagePayload is based on the actual
    // max_packet_length.
    EXPECT_LT(1u, creator_.GetCurrentLargestMessagePayload());
    EXPECT_EQ(overhead, creator_.max_packet_length());
    ASSERT_TRUE(creator_.HasRoomForMessageFrame(
        creator_.GetCurrentLargestMessagePayload()));
    EXPECT_EQ(previous_max_packet_length, creator_.max_packet_length());
  }

  // Verify creator can consume crypto data because max_packet_length_ gets
  // restored.
  creator_.SetSoftMaxPacketLength(overhead);
  EXPECT_EQ(overhead, creator_.max_packet_length());
  std::string data = "crypto data";
  MakeIOVector(data, &iov_);
  QuicFrame frame;
  if (!QuicVersionUsesCryptoFrames(client_framer_.transport_version())) {
    ASSERT_TRUE(creator_.ConsumeDataToFillCurrentPacket(
        QuicUtils::GetCryptoStreamId(client_framer_.transport_version()), &iov_,
        1u, iov_.iov_len, 0u, kOffset, false, true, NOT_RETRANSMISSION,
        &frame));
    size_t bytes_consumed = frame.stream_frame.data_length;
    EXPECT_LT(0u, bytes_consumed);
  } else {
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, kOffset, data);
    ASSERT_TRUE(creator_.ConsumeCryptoDataToFillCurrentPacket(
        ENCRYPTION_INITIAL, data.length(), kOffset,
        /*needs_full_padding=*/true, NOT_RETRANSMISSION, &frame));
    size_t bytes_consumed = frame.crypto_frame->data_length;
    EXPECT_LT(0u, bytes_consumed);
  }
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(Invoke(this, &QuicPacketCreatorTest::SaveSerializedPacket));
  creator_.FlushCurrentPacket();

  // Verify ACK frame can be consumed.
  creator_.SetSoftMaxPacketLength(overhead);
  EXPECT_EQ(overhead, creator_.max_packet_length());
  QuicAckFrame ack_frame(InitAckFrame(10u));
  EXPECT_TRUE(creator_.AddFrame(QuicFrame(&ack_frame), NOT_RETRANSMISSION));
  EXPECT_TRUE(creator_.HasPendingFrames());
}

class MockDelegate : public QuicPacketCreator::DelegateInterface {
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

class MultiplePacketsTestPacketCreator : public QuicPacketCreator {
 public:
  MultiplePacketsTestPacketCreator(
      QuicConnectionId connection_id,
      QuicFramer* framer,
      QuicRandom* random_generator,
      QuicPacketCreator::DelegateInterface* delegate,
      SimpleDataProducer* producer)
      : QuicPacketCreator(connection_id, framer, random_generator, delegate),
        ack_frame_(InitAckFrame(1)),
        delegate_(static_cast<MockDelegate*>(delegate)),
        producer_(producer) {}

  bool ConsumeRetransmittableControlFrame(const QuicFrame& frame,
                                          bool bundle_ack) {
    if (!has_ack()) {
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
    return QuicPacketCreator::ConsumeRetransmittableControlFrame(frame);
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
    return QuicPacketCreator::ConsumeDataFastPath(id, total_length, offset, fin,
                                                  0);
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
    if (!has_ack() && delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                                      NOT_HANDSHAKE)) {
      EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically()).Times(1);
    }
    return QuicPacketCreator::ConsumeData(id, total_length, offset, state);
  }

  MessageStatus AddMessageFrame(QuicMessageId message_id,
                                QuicMemSliceSpan message) {
    if (!has_ack() && delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                                      NOT_HANDSHAKE)) {
      EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically()).Times(1);
    }
    return QuicPacketCreator::AddMessageFrame(message_id, message);
  }

  size_t ConsumeCryptoData(EncryptionLevel level,
                           quiche::QuicheStringPiece data,
                           QuicStreamOffset offset) {
    producer_->SaveCryptoData(level, offset, data);
    if (!has_ack() && delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                                      NOT_HANDSHAKE)) {
      EXPECT_CALL(*delegate_, MaybeBundleAckOpportunistically()).Times(1);
    }
    return QuicPacketCreator::ConsumeCryptoData(level, data.length(), offset);
  }

  QuicAckFrame ack_frame_;
  MockDelegate* delegate_;
  SimpleDataProducer* producer_;
};

class QuicPacketCreatorMultiplePacketsTest : public QuicTest {
 public:
  QuicPacketCreatorMultiplePacketsTest()
      : framer_(AllSupportedVersions(),
                QuicTime::Zero(),
                Perspective::IS_CLIENT,
                kQuicDefaultConnectionIdLength),
        creator_(TestConnectionId(),
                 &framer_,
                 &random_creator_,
                 &delegate_,
                 &producer_),
        ack_frame_(InitAckFrame(1)) {
    EXPECT_CALL(delegate_, GetPacketBuffer()).WillRepeatedly(Return(nullptr));
    creator_.SetEncrypter(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<NullEncrypter>(Perspective::IS_CLIENT));
    creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
    framer_.set_data_producer(&producer_);
    if (simple_framer_.framer()->version().KnowsWhichDecrypterToUse()) {
      simple_framer_.framer()->InstallDecrypter(
          ENCRYPTION_FORWARD_SECURE,
          std::make_unique<NullDecrypter>(Perspective::IS_SERVER));
    }
    creator_.AttachPacketFlusher();
  }

  ~QuicPacketCreatorMultiplePacketsTest() override {
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
  MockRandom random_creator_;
  StrictMock<MockDelegate> delegate_;
  MultiplePacketsTestPacketCreator creator_;
  SimpleQuicFramer simple_framer_;
  std::vector<SerializedPacket> packets_;
  QuicAckFrame ack_frame_;
  struct iovec iov_;
  SimpleBufferAllocator allocator_;

 private:
  std::unique_ptr<char[]> data_array_;
  SimpleDataProducer producer_;
};

TEST_F(QuicPacketCreatorMultiplePacketsTest, AddControlFrame_NotWritable) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/false);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
  delete rst_frame;
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, AddControlFrame_OnlyAckWritable) {
  delegate_.SetCanWriteOnlyNonRetransmittable();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/false);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
  delete rst_frame;
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       AddControlFrame_WritableAndShouldNotFlush) {
  delegate_.SetCanWriteAnything();

  creator_.ConsumeRetransmittableControlFrame(QuicFrame(CreateRstStreamFrame()),
                                              /*bundle_ack=*/false);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       AddControlFrame_NotWritableBatchThenFlush) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/false);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
  delete rst_frame;
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       AddControlFrame_WritableAndShouldFlush) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  creator_.ConsumeRetransmittableControlFrame(QuicFrame(CreateRstStreamFrame()),
                                              /*bundle_ack=*/false);
  creator_.Flush();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  PacketContents contents;
  contents.num_rst_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeCryptoData) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  std::string data = "crypto data";
  size_t consumed_bytes =
      creator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data, 0);
  creator_.Flush();
  EXPECT_EQ(data.length(), consumed_bytes);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  PacketContents contents;
  contents.num_crypto_frames = 1;
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeData_NotWritable) {
  delegate_.SetCanNotWrite();

  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, FIN);
  EXPECT_EQ(0u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       ConsumeData_WritableAndShouldNotFlush) {
  delegate_.SetCanWriteAnything();

  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, FIN);
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       ConsumeData_WritableAndShouldFlush) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, FIN);
  creator_.Flush();
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

// Test the behavior of ConsumeData when the data consumed is for the crypto
// handshake stream.  Ensure that the packet is always sent and padded even if
// the creator operates in batch mode.
TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeData_Handshake) {
  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  std::string data = "foo bar";
  MakeIOVector(data, &iov_);
  size_t consumed_bytes = 0;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    consumed_bytes = creator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data, 0);
  } else {
    consumed_bytes =
        creator_
            .ConsumeData(
                QuicUtils::GetCryptoStreamId(framer_.transport_version()),
                &iov_, 1u, iov_.iov_len, 0, NO_FIN)
            .bytes_consumed;
  }
  EXPECT_EQ(7u, consumed_bytes);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  PacketContents contents;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    contents.num_crypto_frames = 1;
  } else {
    contents.num_stream_frames = 1;
  }
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);

  ASSERT_EQ(1u, packets_.size());
  ASSERT_EQ(kDefaultMaxPacketSize, creator_.max_packet_length());
  EXPECT_EQ(kDefaultMaxPacketSize, packets_[0].encrypted_length);
}

// Test the behavior of ConsumeData when the data is for the crypto handshake
// stream, but padding is disabled.
TEST_F(QuicPacketCreatorMultiplePacketsTest,
       ConsumeData_Handshake_PaddingDisabled) {
  creator_.set_fully_pad_crypto_handshake_packets(false);

  delegate_.SetCanWriteAnything();

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  std::string data = "foo";
  MakeIOVector(data, &iov_);
  size_t bytes_consumed = 0;
  if (QuicVersionUsesCryptoFrames(framer_.transport_version())) {
    bytes_consumed = creator_.ConsumeCryptoData(ENCRYPTION_INITIAL, data, 0);
  } else {
    bytes_consumed =
        creator_
            .ConsumeData(
                QuicUtils::GetCryptoStreamId(framer_.transport_version()),
                &iov_, 1u, iov_.iov_len, 0, NO_FIN)
            .bytes_consumed;
  }
  EXPECT_EQ(3u, bytes_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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
  ASSERT_EQ(kDefaultMaxPacketSize, creator_.max_packet_length());
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

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeData_EmptyData) {
  delegate_.SetCanWriteAnything();

  EXPECT_QUIC_BUG(creator_.ConsumeData(
                      QuicUtils::QuicUtils::GetFirstBidirectionalStreamId(
                          framer_.transport_version(), Perspective::IS_CLIENT),
                      nullptr, 0, 0, 0, NO_FIN),
                  "Attempt to consume empty data without FIN.");
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       ConsumeDataMultipleTimes_WritableAndShouldNotFlush) {
  delegate_.SetCanWriteAnything();

  MakeIOVector("foo", &iov_);
  creator_.ConsumeData(QuicUtils::GetFirstBidirectionalStreamId(
                           framer_.transport_version(), Perspective::IS_CLIENT),
                       &iov_, 1u, iov_.iov_len, 0, FIN);
  MakeIOVector("quux", &iov_);
  QuicConsumedData consumed =
      creator_.ConsumeData(3, &iov_, 1u, iov_.iov_len, 3, NO_FIN);
  EXPECT_EQ(4u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeData_BatchOperations) {
  delegate_.SetCanWriteAnything();

  MakeIOVector("foo", &iov_);
  creator_.ConsumeData(QuicUtils::GetFirstBidirectionalStreamId(
                           framer_.transport_version(), Perspective::IS_CLIENT),
                       &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  MakeIOVector("quux", &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 3, FIN);
  EXPECT_EQ(4u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  // Now both frames will be flushed out.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  creator_.Flush();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       ConsumeData_FramesPreviouslyQueued) {
  // Set the packet size be enough for two stream frames with 0 stream offset,
  // but not enough for a stream frame of 0 offset and one with non-zero offset.
  size_t length =
      NullEncrypter(Perspective::IS_CLIENT).GetCiphertextSize(0) +
      GetPacketHeaderSize(
          framer_.transport_version(),
          creator_.GetDestinationConnectionIdLength(),
          creator_.GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(&creator_)) +
      // Add an extra 3 bytes for the payload and 1 byte so
      // BytesFree is larger than the GetMinStreamFrameSize.
      QuicFramer::GetMinStreamFrameSize(framer_.transport_version(), 1, 0,
                                        false, 3) +
      3 +
      QuicFramer::GetMinStreamFrameSize(framer_.transport_version(), 1, 0, true,
                                        1) +
      1;
  creator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  {
    InSequence dummy;
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(
            Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(
            Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  }
  // Queue enough data to prevent a stream frame with a non-zero offset from
  // fitting.
  MakeIOVector("foo", &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  // This frame will not fit with the existing frame, causing the queued frame
  // to be serialized, and it will be added to a new open packet.
  MakeIOVector("bar", &iov_);
  consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 3, FIN);
  EXPECT_EQ(3u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  creator_.FlushCurrentPacket();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  PacketContents contents;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
  CheckPacketContains(contents, 1);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeDataFastPath) {
  delegate_.SetCanWriteAnything();
  creator_.SetTransmissionType(LOSS_RETRANSMISSION);

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  QuicConsumedData consumed = creator_.ConsumeDataFastPath(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, true);
  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeDataLarge) {
  delegate_.SetCanWriteAnything();

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, FIN);
  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeDataLargeSendAckFalse) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool success =
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/true);
  EXPECT_FALSE(success);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  delegate_.SetCanWriteAnything();

  creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                              /*bundle_ack=*/false);

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  creator_.ConsumeRetransmittableControlFrame(QuicFrame(CreateRstStreamFrame()),
                                              /*bundle_ack=*/true);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, FIN);
  creator_.Flush();

  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  EXPECT_FALSE(packets_.empty());
  SerializedPacket packet = packets_.back();
  EXPECT_TRUE(!packet.retransmittable_frames.empty());
  EXPECT_EQ(STREAM_FRAME, packet.retransmittable_frames.front().type);
  const QuicStreamFrame& stream_frame =
      packet.retransmittable_frames.front().stream_frame;
  EXPECT_EQ(10000u, stream_frame.data_length + stream_frame.offset);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConsumeDataLargeSendAckTrue) {
  delegate_.SetCanNotWrite();
  delegate_.SetCanWriteAnything();

  // Create a 10000 byte IOVector.
  CreateData(10000);
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, 0, FIN);
  creator_.Flush();

  EXPECT_EQ(10000u, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  EXPECT_FALSE(packets_.empty());
  SerializedPacket packet = packets_.back();
  EXPECT_TRUE(!packet.retransmittable_frames.empty());
  EXPECT_EQ(STREAM_FRAME, packet.retransmittable_frames.front().type);
  const QuicStreamFrame& stream_frame =
      packet.retransmittable_frames.front().stream_frame;
  EXPECT_EQ(10000u, stream_frame.data_length + stream_frame.offset);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, NotWritableThenBatchOperations) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool consumed =
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/true);
  EXPECT_FALSE(consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(3));

  delegate_.SetCanWriteAnything();

  EXPECT_TRUE(
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/false));
  // Send some data and a control frame
  MakeIOVector("quux", &iov_);
  creator_.ConsumeData(3, &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  if (!VersionHasIetfQuicFrames(framer_.transport_version())) {
    creator_.ConsumeRetransmittableControlFrame(QuicFrame(CreateGoAwayFrame()),
                                                /*bundle_ack=*/false);
  }
  EXPECT_TRUE(creator_.HasPendingStreamFramesOfStream(3));

  // All five frames will be flushed out in a single packet.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  creator_.Flush();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
  EXPECT_FALSE(creator_.HasPendingStreamFramesOfStream(3));

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

TEST_F(QuicPacketCreatorMultiplePacketsTest, NotWritableThenBatchOperations2) {
  delegate_.SetCanNotWrite();

  QuicRstStreamFrame* rst_frame = CreateRstStreamFrame();
  const bool success =
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/true);
  EXPECT_FALSE(success);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  delegate_.SetCanWriteAnything();

  {
    InSequence dummy;
    // All five frames will be flushed out in a single packet
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(
            Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
    EXPECT_CALL(delegate_, OnSerializedPacket(_))
        .WillOnce(
            Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  }
  EXPECT_TRUE(
      creator_.ConsumeRetransmittableControlFrame(QuicFrame(rst_frame),
                                                  /*bundle_ack=*/false));
  // Send enough data to exceed one packet
  size_t data_len = kDefaultMaxPacketSize + 100;
  CreateData(data_len);
  QuicConsumedData consumed =
      creator_.ConsumeData(3, &iov_, 1u, iov_.iov_len, 0, FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  if (!VersionHasIetfQuicFrames(framer_.transport_version())) {
    creator_.ConsumeRetransmittableControlFrame(QuicFrame(CreateGoAwayFrame()),
                                                /*bundle_ack=*/false);
  }

  creator_.Flush();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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
TEST_F(QuicPacketCreatorMultiplePacketsTest, PacketTransmissionType) {
  delegate_.SetCanWriteAnything();

  // The first ConsumeData will fill the packet without flush.
  creator_.SetTransmissionType(LOSS_RETRANSMISSION);

  size_t data_len = 1324;
  CreateData(data_len);
  QuicStreamId stream1_id = QuicUtils::GetFirstBidirectionalStreamId(
      framer_.transport_version(), Perspective::IS_CLIENT);
  QuicConsumedData consumed =
      creator_.ConsumeData(stream1_id, &iov_, 1u, iov_.iov_len, 0, NO_FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  ASSERT_EQ(0u, creator_.BytesFree())
      << "Test setup failed: Please increase data_len to "
      << data_len + creator_.BytesFree() << " bytes.";

  // The second ConsumeData can not be added to the packet and will flush.
  creator_.SetTransmissionType(NOT_RETRANSMISSION);

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  QuicStreamId stream2_id = stream1_id + 4;

  consumed =
      creator_.ConsumeData(stream2_id, &iov_, 1u, iov_.iov_len, 0, NO_FIN);
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

TEST_F(QuicPacketCreatorMultiplePacketsTest, TestConnectionIdLength) {
  QuicFramerPeer::SetPerspective(&framer_, Perspective::IS_SERVER);
  creator_.SetServerConnectionIdLength(0);
  EXPECT_EQ(PACKET_0BYTE_CONNECTION_ID,
            creator_.GetDestinationConnectionIdLength());

  for (size_t i = 1; i < 10; i++) {
    creator_.SetServerConnectionIdLength(i);
    if (VersionHasIetfInvariantHeader(framer_.transport_version())) {
      EXPECT_EQ(PACKET_0BYTE_CONNECTION_ID,
                creator_.GetDestinationConnectionIdLength());
    } else {
      EXPECT_EQ(PACKET_8BYTE_CONNECTION_ID,
                creator_.GetDestinationConnectionIdLength());
    }
  }
}

// Test whether SetMaxPacketLength() works in the situation when the queue is
// empty, and we send three packets worth of data.
TEST_F(QuicPacketCreatorMultiplePacketsTest, SetMaxPacketLength_Initial) {
  delegate_.SetCanWriteAnything();

  // Send enough data for three packets.
  size_t data_len = 3 * kDefaultMaxPacketSize + 1;
  size_t packet_len = kDefaultMaxPacketSize + 100;
  ASSERT_LE(packet_len, kMaxOutgoingPacketSize);
  creator_.SetMaxPacketLength(packet_len);
  EXPECT_EQ(packet_len, creator_.max_packet_length());

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .Times(3)
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  CreateData(data_len);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len,
      /*offset=*/0, FIN);
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  // We expect three packets, and first two of them have to be of packet_len
  // size.  We check multiple packets (instead of just one) because we want to
  // ensure that |max_packet_length_| does not get changed incorrectly by the
  // creator after first packet is serialized.
  ASSERT_EQ(3u, packets_.size());
  EXPECT_EQ(packet_len, packets_[0].encrypted_length);
  EXPECT_EQ(packet_len, packets_[1].encrypted_length);
  CheckAllPacketsHaveSingleStreamFrame();
}

// Test whether SetMaxPacketLength() works in the situation when we first write
// data, then change packet size, then write data again.
TEST_F(QuicPacketCreatorMultiplePacketsTest, SetMaxPacketLength_Middle) {
  delegate_.SetCanWriteAnything();

  // We send enough data to overflow default packet length, but not the altered
  // one.
  size_t data_len = kDefaultMaxPacketSize;
  size_t packet_len = kDefaultMaxPacketSize + 100;
  ASSERT_LE(packet_len, kMaxOutgoingPacketSize);

  // We expect to see three packets in total.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .Times(3)
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  // Send two packets before packet size change.
  CreateData(data_len);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len,
      /*offset=*/0, NO_FIN);
  creator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  // Make sure we already have two packets.
  ASSERT_EQ(2u, packets_.size());

  // Increase packet size.
  creator_.SetMaxPacketLength(packet_len);
  EXPECT_EQ(packet_len, creator_.max_packet_length());

  // Send a packet after packet size change.
  CreateData(data_len);
  creator_.AttachPacketFlusher();
  consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len, data_len, FIN);
  creator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  // We expect first data chunk to get fragmented, but the second one to fit
  // into a single packet.
  ASSERT_EQ(3u, packets_.size());
  EXPECT_EQ(kDefaultMaxPacketSize, packets_[0].encrypted_length);
  EXPECT_LE(kDefaultMaxPacketSize, packets_[2].encrypted_length);
  CheckAllPacketsHaveSingleStreamFrame();
}

// Test whether SetMaxPacketLength() works correctly when we force the change of
// the packet size in the middle of the batched packet.
TEST_F(QuicPacketCreatorMultiplePacketsTest,
       SetMaxPacketLength_MidpacketFlush) {
  delegate_.SetCanWriteAnything();

  size_t first_write_len = kDefaultMaxPacketSize / 2;
  size_t packet_len = kDefaultMaxPacketSize + 100;
  size_t second_write_len = packet_len + 1;
  ASSERT_LE(packet_len, kMaxOutgoingPacketSize);

  // First send half of the packet worth of data.  We are in the batch mode, so
  // should not cause packet serialization.
  CreateData(first_write_len);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len,
      /*offset=*/0, NO_FIN);
  EXPECT_EQ(first_write_len, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  // Make sure we have no packets so far.
  ASSERT_EQ(0u, packets_.size());

  // Expect a packet to be flushed.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  // Increase packet size after flushing all frames.
  // Ensure it's immediately enacted.
  creator_.FlushCurrentPacket();
  creator_.SetMaxPacketLength(packet_len);
  EXPECT_EQ(packet_len, creator_.max_packet_length());
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  // We expect to see exactly one packet serialized after that, because we send
  // a value somewhat exceeding new max packet size, and the tail data does not
  // get serialized because we are still in the batch mode.
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  // Send a more than a packet worth of data to the same stream.  This should
  // trigger serialization of one packet, and queue another one.
  CreateData(second_write_len);
  consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len,
      /*offset=*/first_write_len, FIN);
  EXPECT_EQ(second_write_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  // We expect the first packet to be underfilled, and the second packet be up
  // to the new max packet size.
  ASSERT_EQ(2u, packets_.size());
  EXPECT_GT(kDefaultMaxPacketSize, packets_[0].encrypted_length);
  EXPECT_EQ(packet_len, packets_[1].encrypted_length);

  CheckAllPacketsHaveSingleStreamFrame();
}

// Test sending a connectivity probing packet.
TEST_F(QuicPacketCreatorMultiplePacketsTest,
       GenerateConnectivityProbingPacket) {
  delegate_.SetCanWriteAnything();

  OwningSerializedPacketPointer probing_packet;
  if (VersionHasIetfQuicFrames(framer_.transport_version())) {
    QuicPathFrameBuffer payload = {
        {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xfe}};
    probing_packet =
        creator_.SerializePathChallengeConnectivityProbingPacket(&payload);
  } else {
    probing_packet = creator_.SerializeConnectivityProbingPacket();
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
TEST_F(QuicPacketCreatorMultiplePacketsTest,
       GenerateMtuDiscoveryPacket_Simple) {
  delegate_.SetCanWriteAnything();

  const size_t target_mtu = kDefaultMaxPacketSize + 100;
  static_assert(target_mtu < kMaxOutgoingPacketSize,
                "The MTU probe used by the test exceeds maximum packet size");

  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  creator_.GenerateMtuDiscoveryPacket(target_mtu);

  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());
  ASSERT_EQ(1u, packets_.size());
  EXPECT_EQ(target_mtu, packets_[0].encrypted_length);

  PacketContents contents;
  contents.num_mtu_discovery_frames = 1;
  contents.num_padding_frames = 1;
  CheckPacketContains(contents, 0);
}

// Test sending an MTU probe.  Surround it with data, to ensure that it resets
// the MTU to the value before the probe was sent.
TEST_F(QuicPacketCreatorMultiplePacketsTest,
       GenerateMtuDiscoveryPacket_SurroundedByData) {
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
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  // Send data before the MTU probe.
  CreateData(data_len);
  QuicConsumedData consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len,
      /*offset=*/0, NO_FIN);
  creator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_FALSE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  // Send the MTU probe.
  creator_.GenerateMtuDiscoveryPacket(target_mtu);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  // Send data after the MTU probe.
  CreateData(data_len);
  creator_.AttachPacketFlusher();
  consumed = creator_.ConsumeData(
      QuicUtils::GetFirstBidirectionalStreamId(framer_.transport_version(),
                                               Perspective::IS_CLIENT),
      &iov_, 1u, iov_.iov_len,
      /*offset=*/data_len, FIN);
  creator_.Flush();
  EXPECT_EQ(data_len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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

TEST_F(QuicPacketCreatorMultiplePacketsTest, DontCrashOnInvalidStopWaiting) {
  if (VersionSupportsMessageFrames(framer_.transport_version())) {
    return;
  }
  // Test added to ensure the creator does not crash when an invalid frame is
  // added.  Because this is an indication of internal programming errors,
  // DFATALs are expected.
  // A 1 byte packet number length can't encode a gap of 1000.
  QuicPacketCreatorPeer::SetPacketNumber(&creator_, 1000);

  delegate_.SetCanNotWrite();
  delegate_.SetCanWriteAnything();

  // This will not serialize any packets, because of the invalid frame.
  EXPECT_CALL(delegate_,
              OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET, _));
  EXPECT_QUIC_BUG(creator_.Flush(),
                  "packet_number_length 1 is too small "
                  "for least_unacked_delta: 1001");
}

// Regression test for b/31486443.
TEST_F(QuicPacketCreatorMultiplePacketsTest,
       ConnectionCloseFrameLargerThanPacketSize) {
  delegate_.SetCanWriteAnything();
  char buf[2000] = {};
  quiche::QuicheStringPiece error_details(buf, 2000);
  const QuicErrorCode kQuicErrorCode = QUIC_PACKET_WRITE_ERROR;

  QuicConnectionCloseFrame* frame = new QuicConnectionCloseFrame(
      framer_.transport_version(), kQuicErrorCode, std::string(error_details),
      /*transport_close_frame_type=*/0);
  creator_.ConsumeRetransmittableControlFrame(QuicFrame(frame),
                                              /*bundle_ack=*/false);
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
       RandomPaddingAfterFinSingleStreamSinglePacket) {
  const QuicByteCount kStreamFramePayloadSize = 100u;
  char buf[kStreamFramePayloadSize] = {};
  const QuicStreamId kDataStreamId = 5;
  // Set the packet size be enough for one stream frame with 0 stream offset and
  // max size of random padding.
  size_t length =
      NullEncrypter(Perspective::IS_CLIENT).GetCiphertextSize(0) +
      GetPacketHeaderSize(
          framer_.transport_version(),
          creator_.GetDestinationConnectionIdLength(),
          creator_.GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(&creator_)) +
      QuicFramer::GetMinStreamFrameSize(
          framer_.transport_version(), kDataStreamId, 0,
          /*last_frame_in_packet=*/false,
          kStreamFramePayloadSize + kMaxNumRandomPaddingBytes) +
      kStreamFramePayloadSize + kMaxNumRandomPaddingBytes;
  creator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  MakeIOVector(quiche::QuicheStringPiece(buf, kStreamFramePayloadSize), &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      kDataStreamId, &iov_, 1u, iov_.iov_len, 0, FIN_AND_PADDING);
  creator_.Flush();
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

  EXPECT_EQ(1u, packets_.size());
  PacketContents contents;
  // The packet has both stream and padding frames.
  contents.num_padding_frames = 1;
  contents.num_stream_frames = 1;
  CheckPacketContains(contents, 0);
}

TEST_F(QuicPacketCreatorMultiplePacketsTest,
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
          creator_.GetDestinationConnectionIdLength(),
          creator_.GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(&creator_)) +
      QuicFramer::GetMinStreamFrameSize(
          framer_.transport_version(), kDataStreamId, 0,
          /*last_frame_in_packet=*/false, kStreamFramePayloadSize + 1) +
      kStreamFramePayloadSize + 1;
  creator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  MakeIOVector(quiche::QuicheStringPiece(buf, kStreamFramePayloadSize), &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      kDataStreamId, &iov_, 1u, iov_.iov_len, 0, FIN_AND_PADDING);
  creator_.Flush();
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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

TEST_F(QuicPacketCreatorMultiplePacketsTest,
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
          creator_.GetDestinationConnectionIdLength(),
          creator_.GetSourceConnectionIdLength(),
          QuicPacketCreatorPeer::SendVersionInPacket(&creator_),
          !kIncludeDiversificationNonce,
          QuicPacketCreatorPeer::GetPacketNumberLength(&creator_),
          QuicPacketCreatorPeer::GetRetryTokenLengthLength(&creator_), 0,
          QuicPacketCreatorPeer::GetLengthLength(&creator_)) +
      QuicFramer::GetMinStreamFrameSize(
          framer_.transport_version(), kDataStreamId1, 0,
          /*last_frame_in_packet=*/false, kStreamFramePayloadSize) +
      kStreamFramePayloadSize +
      QuicFramer::GetMinStreamFrameSize(framer_.transport_version(),
                                        kDataStreamId1, 0,
                                        /*last_frame_in_packet=*/false, 1) +
      1;
  creator_.SetMaxPacketLength(length);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillRepeatedly(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));
  MakeIOVector(quiche::QuicheStringPiece(buf, kStreamFramePayloadSize), &iov_);
  QuicConsumedData consumed = creator_.ConsumeData(
      kDataStreamId1, &iov_, 1u, iov_.iov_len, 0, FIN_AND_PADDING);
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  MakeIOVector(quiche::QuicheStringPiece(buf, kStreamFramePayloadSize), &iov_);
  consumed = creator_.ConsumeData(kDataStreamId2, &iov_, 1u, iov_.iov_len, 0,
                                  FIN_AND_PADDING);
  EXPECT_EQ(kStreamFramePayloadSize, consumed.bytes_consumed);
  creator_.Flush();
  EXPECT_FALSE(creator_.HasPendingFrames());
  EXPECT_FALSE(creator_.HasPendingRetransmittableFrames());

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

TEST_F(QuicPacketCreatorMultiplePacketsTest, AddMessageFrame) {
  if (!VersionSupportsMessageFrames(framer_.transport_version())) {
    return;
  }
  quic::QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  delegate_.SetCanWriteAnything();
  EXPECT_CALL(delegate_, OnSerializedPacket(_))
      .WillOnce(
          Invoke(this, &QuicPacketCreatorMultiplePacketsTest::SavePacket));

  MakeIOVector("foo", &iov_);
  creator_.ConsumeData(QuicUtils::GetFirstBidirectionalStreamId(
                           framer_.transport_version(), Perspective::IS_CLIENT),
                       &iov_, 1u, iov_.iov_len, 0, FIN);
  EXPECT_EQ(
      MESSAGE_STATUS_SUCCESS,
      creator_.AddMessageFrame(1, MakeSpan(&allocator_, "message", &storage)));
  EXPECT_TRUE(creator_.HasPendingFrames());
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  // Add a message which causes the flush of current packet.
  EXPECT_EQ(
      MESSAGE_STATUS_SUCCESS,
      creator_.AddMessageFrame(
          2,
          MakeSpan(&allocator_,
                   std::string(creator_.GetCurrentLargestMessagePayload(), 'a'),
                   &storage)));
  EXPECT_TRUE(creator_.HasPendingRetransmittableFrames());

  // Failed to send messages which cannot fit into one packet.
  EXPECT_EQ(
      MESSAGE_STATUS_TOO_LARGE,
      creator_.AddMessageFrame(
          3, MakeSpan(&allocator_,
                      std::string(
                          creator_.GetCurrentLargestMessagePayload() + 10, 'a'),
                      &storage)));
}

TEST_F(QuicPacketCreatorMultiplePacketsTest, ConnectionId) {
  creator_.SetServerConnectionId(TestConnectionId(0x1337));
  EXPECT_EQ(TestConnectionId(0x1337), creator_.GetDestinationConnectionId());
  EXPECT_EQ(EmptyQuicConnectionId(), creator_.GetSourceConnectionId());
  if (!framer_.version().SupportsClientConnectionIds()) {
    return;
  }
  creator_.SetClientConnectionId(TestConnectionId(0x33));
  EXPECT_EQ(TestConnectionId(0x1337), creator_.GetDestinationConnectionId());
  EXPECT_EQ(TestConnectionId(0x33), creator_.GetSourceConnectionId());
}

}  // namespace
}  // namespace test
}  // namespace quic
