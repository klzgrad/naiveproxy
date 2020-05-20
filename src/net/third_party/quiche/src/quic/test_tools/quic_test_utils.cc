// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "third_party/boringssl/src/include/openssl/chacha.h"
#include "third_party/boringssl/src/include/openssl/sha.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_frame_builder.h"

using testing::_;
using testing::Invoke;

namespace quic {
namespace test {

QuicConnectionId TestConnectionId() {
  // Chosen by fair dice roll.
  // Guaranteed to be random.
  return TestConnectionId(42);
}

QuicConnectionId TestConnectionId(uint64_t connection_number) {
  const uint64_t connection_id64_net =
      quiche::QuicheEndian::HostToNet64(connection_number);
  return QuicConnectionId(reinterpret_cast<const char*>(&connection_id64_net),
                          sizeof(connection_id64_net));
}

QuicConnectionId TestConnectionIdNineBytesLong(uint64_t connection_number) {
  const uint64_t connection_number_net =
      quiche::QuicheEndian::HostToNet64(connection_number);
  char connection_id_bytes[9] = {};
  static_assert(
      sizeof(connection_id_bytes) == 1 + sizeof(connection_number_net),
      "bad lengths");
  memcpy(connection_id_bytes + 1, &connection_number_net,
         sizeof(connection_number_net));
  return QuicConnectionId(connection_id_bytes, sizeof(connection_id_bytes));
}

uint64_t TestConnectionIdToUInt64(QuicConnectionId connection_id) {
  DCHECK_EQ(connection_id.length(), kQuicDefaultConnectionIdLength);
  uint64_t connection_id64_net = 0;
  memcpy(&connection_id64_net, connection_id.data(),
         std::min<size_t>(static_cast<size_t>(connection_id.length()),
                          sizeof(connection_id64_net)));
  return quiche::QuicheEndian::NetToHost64(connection_id64_net);
}

QuicAckFrame InitAckFrame(const std::vector<QuicAckBlock>& ack_blocks) {
  DCHECK_GT(ack_blocks.size(), 0u);

  QuicAckFrame ack;
  QuicPacketNumber end_of_previous_block(1);
  for (const QuicAckBlock& block : ack_blocks) {
    DCHECK_GE(block.start, end_of_previous_block);
    DCHECK_GT(block.limit, block.start);
    ack.packets.AddRange(block.start, block.limit);
    end_of_previous_block = block.limit;
  }

  ack.largest_acked = ack.packets.Max();

  return ack;
}

QuicAckFrame InitAckFrame(uint64_t largest_acked) {
  return InitAckFrame(QuicPacketNumber(largest_acked));
}

QuicAckFrame InitAckFrame(QuicPacketNumber largest_acked) {
  return InitAckFrame({{QuicPacketNumber(1), largest_acked + 1}});
}

QuicAckFrame MakeAckFrameWithAckBlocks(size_t num_ack_blocks,
                                       uint64_t least_unacked) {
  QuicAckFrame ack;
  ack.largest_acked = QuicPacketNumber(2 * num_ack_blocks + least_unacked);
  // Add enough received packets to get num_ack_blocks ack blocks.
  for (QuicPacketNumber i = QuicPacketNumber(2);
       i < QuicPacketNumber(2 * num_ack_blocks + 1); i += 2) {
    ack.packets.Add(i + least_unacked);
  }
  return ack;
}

QuicAckFrame MakeAckFrameWithGaps(uint64_t gap_size,
                                  size_t max_num_gaps,
                                  uint64_t largest_acked) {
  QuicAckFrame ack;
  ack.largest_acked = QuicPacketNumber(largest_acked);
  ack.packets.Add(QuicPacketNumber(largest_acked));
  for (size_t i = 0; i < max_num_gaps; ++i) {
    if (largest_acked <= gap_size) {
      break;
    }
    largest_acked -= gap_size;
    ack.packets.Add(QuicPacketNumber(largest_acked));
  }
  return ack;
}

EncryptionLevel HeaderToEncryptionLevel(const QuicPacketHeader& header) {
  if (header.form == IETF_QUIC_SHORT_HEADER_PACKET) {
    return ENCRYPTION_FORWARD_SECURE;
  } else if (header.form == IETF_QUIC_LONG_HEADER_PACKET) {
    if (header.long_packet_type == HANDSHAKE) {
      return ENCRYPTION_HANDSHAKE;
    } else if (header.long_packet_type == ZERO_RTT_PROTECTED) {
      return ENCRYPTION_ZERO_RTT;
    }
  }
  return ENCRYPTION_INITIAL;
}

std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer,
    const QuicPacketHeader& header,
    const QuicFrames& frames) {
  const size_t max_plaintext_size =
      framer->GetMaxPlaintextSize(kMaxOutgoingPacketSize);
  size_t packet_size = GetPacketHeaderSize(framer->transport_version(), header);
  for (size_t i = 0; i < frames.size(); ++i) {
    DCHECK_LE(packet_size, max_plaintext_size);
    bool first_frame = i == 0;
    bool last_frame = i == frames.size() - 1;
    const size_t frame_size = framer->GetSerializedFrameLength(
        frames[i], max_plaintext_size - packet_size, first_frame, last_frame,
        header.packet_number_length);
    DCHECK(frame_size);
    packet_size += frame_size;
  }
  return BuildUnsizedDataPacket(framer, header, frames, packet_size);
}

std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer,
    const QuicPacketHeader& header,
    const QuicFrames& frames,
    size_t packet_size) {
  char* buffer = new char[packet_size];
  EncryptionLevel level = HeaderToEncryptionLevel(header);
  size_t length =
      framer->BuildDataPacket(header, frames, buffer, packet_size, level);
  DCHECK_NE(0u, length);
  // Re-construct the data packet with data ownership.
  return std::make_unique<QuicPacket>(
      buffer, length, /* owns_buffer */ true,
      GetIncludedDestinationConnectionIdLength(header),
      GetIncludedSourceConnectionIdLength(header), header.version_flag,
      header.nonce != nullptr, header.packet_number_length,
      header.retry_token_length_length, header.retry_token.length(),
      header.length_length);
}

std::string Sha1Hash(quiche::QuicheStringPiece data) {
  char buffer[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
       reinterpret_cast<uint8_t*>(buffer));
  return std::string(buffer, QUICHE_ARRAYSIZE(buffer));
}

bool ClearControlFrame(const QuicFrame& frame) {
  DeleteFrame(&const_cast<QuicFrame&>(frame));
  return true;
}

uint64_t SimpleRandom::RandUint64() {
  uint64_t result;
  RandBytes(&result, sizeof(result));
  return result;
}

void SimpleRandom::RandBytes(void* data, size_t len) {
  uint8_t* data_bytes = reinterpret_cast<uint8_t*>(data);
  while (len > 0) {
    const size_t buffer_left = sizeof(buffer_) - buffer_offset_;
    const size_t to_copy = std::min(buffer_left, len);
    memcpy(data_bytes, buffer_ + buffer_offset_, to_copy);
    data_bytes += to_copy;
    buffer_offset_ += to_copy;
    len -= to_copy;

    if (buffer_offset_ == sizeof(buffer_)) {
      FillBuffer();
    }
  }
}

void SimpleRandom::FillBuffer() {
  uint8_t nonce[12];
  memcpy(nonce, buffer_, sizeof(nonce));
  CRYPTO_chacha_20(buffer_, buffer_, sizeof(buffer_), key_, nonce, 0);
  buffer_offset_ = 0;
}

void SimpleRandom::set_seed(uint64_t seed) {
  static_assert(sizeof(key_) == SHA256_DIGEST_LENGTH, "Key has to be 256 bits");
  SHA256(reinterpret_cast<const uint8_t*>(&seed), sizeof(seed), key_);

  memset(buffer_, 0, sizeof(buffer_));
  FillBuffer();
}

MockFramerVisitor::MockFramerVisitor() {
  // By default, we want to accept packets.
  ON_CALL(*this, OnProtocolVersionMismatch(_))
      .WillByDefault(testing::Return(false));

  // By default, we want to accept packets.
  ON_CALL(*this, OnUnauthenticatedHeader(_))
      .WillByDefault(testing::Return(true));

  ON_CALL(*this, OnUnauthenticatedPublicHeader(_))
      .WillByDefault(testing::Return(true));

  ON_CALL(*this, OnPacketHeader(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnStreamFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnCryptoFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnStopWaitingFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnPaddingFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnPingFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnRstStreamFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnConnectionCloseFrame(_))
      .WillByDefault(testing::Return(true));

  ON_CALL(*this, OnStopSendingFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnPathChallengeFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnPathResponseFrame(_)).WillByDefault(testing::Return(true));

  ON_CALL(*this, OnGoAwayFrame(_)).WillByDefault(testing::Return(true));
  ON_CALL(*this, OnMaxStreamsFrame(_)).WillByDefault(testing::Return(true));
  ON_CALL(*this, OnStreamsBlockedFrame(_)).WillByDefault(testing::Return(true));
}

MockFramerVisitor::~MockFramerVisitor() {}

bool NoOpFramerVisitor::OnProtocolVersionMismatch(
    ParsedQuicVersion /*version*/) {
  return false;
}

bool NoOpFramerVisitor::OnUnauthenticatedPublicHeader(
    const QuicPacketHeader& /*header*/) {
  return true;
}

bool NoOpFramerVisitor::OnUnauthenticatedHeader(
    const QuicPacketHeader& /*header*/) {
  return true;
}

bool NoOpFramerVisitor::OnPacketHeader(const QuicPacketHeader& /*header*/) {
  return true;
}

void NoOpFramerVisitor::OnCoalescedPacket(
    const QuicEncryptedPacket& /*packet*/) {}

void NoOpFramerVisitor::OnUndecryptablePacket(
    const QuicEncryptedPacket& /*packet*/,
    EncryptionLevel /*decryption_level*/,
    bool /*has_decryption_key*/) {}

bool NoOpFramerVisitor::OnStreamFrame(const QuicStreamFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnCryptoFrame(const QuicCryptoFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnAckFrameStart(QuicPacketNumber /*largest_acked*/,
                                        QuicTime::Delta /*ack_delay_time*/) {
  return true;
}

bool NoOpFramerVisitor::OnAckRange(QuicPacketNumber /*start*/,
                                   QuicPacketNumber /*end*/) {
  return true;
}

bool NoOpFramerVisitor::OnAckTimestamp(QuicPacketNumber /*packet_number*/,
                                       QuicTime /*timestamp*/) {
  return true;
}

bool NoOpFramerVisitor::OnAckFrameEnd(QuicPacketNumber /*start*/) {
  return true;
}

bool NoOpFramerVisitor::OnStopWaitingFrame(
    const QuicStopWaitingFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnPaddingFrame(const QuicPaddingFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnPingFrame(const QuicPingFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnRstStreamFrame(const QuicRstStreamFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnConnectionCloseFrame(
    const QuicConnectionCloseFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnNewTokenFrame(const QuicNewTokenFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnStopSendingFrame(
    const QuicStopSendingFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnPathChallengeFrame(
    const QuicPathChallengeFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnPathResponseFrame(
    const QuicPathResponseFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnGoAwayFrame(const QuicGoAwayFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnMaxStreamsFrame(
    const QuicMaxStreamsFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnWindowUpdateFrame(
    const QuicWindowUpdateFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnBlockedFrame(const QuicBlockedFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnMessageFrame(const QuicMessageFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::OnHandshakeDoneFrame(
    const QuicHandshakeDoneFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::IsValidStatelessResetToken(
    QuicUint128 /*token*/) const {
  return false;
}

MockQuicConnectionVisitor::MockQuicConnectionVisitor() {}

MockQuicConnectionVisitor::~MockQuicConnectionVisitor() {}

MockQuicConnectionHelper::MockQuicConnectionHelper() {}

MockQuicConnectionHelper::~MockQuicConnectionHelper() {}

const QuicClock* MockQuicConnectionHelper::GetClock() const {
  return &clock_;
}

QuicRandom* MockQuicConnectionHelper::GetRandomGenerator() {
  return &random_generator_;
}

QuicAlarm* MockAlarmFactory::CreateAlarm(QuicAlarm::Delegate* delegate) {
  return new MockAlarmFactory::TestAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
}

QuicArenaScopedPtr<QuicAlarm> MockAlarmFactory::CreateAlarm(
    QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
    QuicConnectionArena* arena) {
  if (arena != nullptr) {
    return arena->New<TestAlarm>(std::move(delegate));
  } else {
    return QuicArenaScopedPtr<TestAlarm>(new TestAlarm(std::move(delegate)));
  }
}

QuicBufferAllocator* MockQuicConnectionHelper::GetStreamSendBufferAllocator() {
  return &buffer_allocator_;
}

void MockQuicConnectionHelper::AdvanceTime(QuicTime::Delta delta) {
  clock_.AdvanceTime(delta);
}

MockQuicConnection::MockQuicConnection(MockQuicConnectionHelper* helper,
                                       MockAlarmFactory* alarm_factory,
                                       Perspective perspective)
    : MockQuicConnection(TestConnectionId(),
                         QuicSocketAddress(TestPeerIPAddress(), kTestPort),
                         helper,
                         alarm_factory,
                         perspective,
                         ParsedVersionOfIndex(CurrentSupportedVersions(), 0)) {}

MockQuicConnection::MockQuicConnection(QuicSocketAddress address,
                                       MockQuicConnectionHelper* helper,
                                       MockAlarmFactory* alarm_factory,
                                       Perspective perspective)
    : MockQuicConnection(TestConnectionId(),
                         address,
                         helper,
                         alarm_factory,
                         perspective,
                         ParsedVersionOfIndex(CurrentSupportedVersions(), 0)) {}

MockQuicConnection::MockQuicConnection(QuicConnectionId connection_id,
                                       MockQuicConnectionHelper* helper,
                                       MockAlarmFactory* alarm_factory,
                                       Perspective perspective)
    : MockQuicConnection(connection_id,
                         QuicSocketAddress(TestPeerIPAddress(), kTestPort),
                         helper,
                         alarm_factory,
                         perspective,
                         ParsedVersionOfIndex(CurrentSupportedVersions(), 0)) {}

MockQuicConnection::MockQuicConnection(
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
    Perspective perspective,
    const ParsedQuicVersionVector& supported_versions)
    : MockQuicConnection(TestConnectionId(),
                         QuicSocketAddress(TestPeerIPAddress(), kTestPort),
                         helper,
                         alarm_factory,
                         perspective,
                         supported_versions) {}

MockQuicConnection::MockQuicConnection(
    QuicConnectionId connection_id,
    QuicSocketAddress address,
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
    Perspective perspective,
    const ParsedQuicVersionVector& supported_versions)
    : QuicConnection(connection_id,
                     address,
                     helper,
                     alarm_factory,
                     new testing::NiceMock<MockPacketWriter>(),
                     /* owns_writer= */ true,
                     perspective,
                     supported_versions) {
  ON_CALL(*this, OnError(_))
      .WillByDefault(
          Invoke(this, &PacketSavingConnection::QuicConnection_OnError));
  ON_CALL(*this, SendCryptoData(_, _, _))
      .WillByDefault(
          Invoke(this, &MockQuicConnection::QuicConnection_SendCryptoData));

  SetSelfAddress(QuicSocketAddress(QuicIpAddress::Any4(), 5));
}

MockQuicConnection::~MockQuicConnection() {}

void MockQuicConnection::AdvanceTime(QuicTime::Delta delta) {
  static_cast<MockQuicConnectionHelper*>(helper())->AdvanceTime(delta);
}

bool MockQuicConnection::OnProtocolVersionMismatch(
    ParsedQuicVersion /*version*/) {
  return false;
}

PacketSavingConnection::PacketSavingConnection(MockQuicConnectionHelper* helper,
                                               MockAlarmFactory* alarm_factory,
                                               Perspective perspective)
    : MockQuicConnection(helper, alarm_factory, perspective) {}

PacketSavingConnection::PacketSavingConnection(
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
    Perspective perspective,
    const ParsedQuicVersionVector& supported_versions)
    : MockQuicConnection(helper,
                         alarm_factory,
                         perspective,
                         supported_versions) {}

PacketSavingConnection::~PacketSavingConnection() {}

void PacketSavingConnection::SendOrQueuePacket(SerializedPacket* packet) {
  encrypted_packets_.push_back(std::make_unique<QuicEncryptedPacket>(
      CopyBuffer(*packet), packet->encrypted_length, true));
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  // Transfer ownership of the packet to the SentPacketManager and the
  // ack notifier to the AckNotifierManager.
  QuicConnectionPeer::GetSentPacketManager(this)->OnPacketSent(
      packet, clock_.ApproximateNow(), NOT_RETRANSMISSION,
      HAS_RETRANSMITTABLE_DATA);
}

MockQuicSession::MockQuicSession(QuicConnection* connection)
    : MockQuicSession(connection, true) {}

MockQuicSession::MockQuicSession(QuicConnection* connection,
                                 bool create_mock_crypto_stream)
    : QuicSession(connection,
                  nullptr,
                  DefaultQuicConfig(),
                  connection->supported_versions(),
                  /*num_expected_unidirectional_static_streams = */ 0) {
  if (create_mock_crypto_stream) {
    crypto_stream_ = std::make_unique<MockQuicCryptoStream>(this);
  }
  ON_CALL(*this, WritevData(_, _, _, _, _, _))
      .WillByDefault(testing::Return(QuicConsumedData(0, false)));
}

MockQuicSession::~MockQuicSession() {
  DeleteConnection();
}

QuicCryptoStream* MockQuicSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoStream* MockQuicSession::GetCryptoStream() const {
  return crypto_stream_.get();
}

void MockQuicSession::SetCryptoStream(QuicCryptoStream* crypto_stream) {
  crypto_stream_.reset(crypto_stream);
}

QuicConsumedData MockQuicSession::ConsumeData(
    QuicStreamId id,
    size_t write_length,
    QuicStreamOffset offset,
    StreamSendingState state,
    TransmissionType /*type*/,
    quiche::QuicheOptional<EncryptionLevel> /*level*/) {
  if (write_length > 0) {
    auto buf = std::make_unique<char[]>(write_length);
    QuicStream* stream = GetOrCreateStream(id);
    DCHECK(stream);
    QuicDataWriter writer(write_length, buf.get(), quiche::HOST_BYTE_ORDER);
    stream->WriteStreamData(offset, write_length, &writer);
  } else {
    DCHECK(state != NO_FIN);
  }
  return QuicConsumedData(write_length, state != NO_FIN);
}

MockQuicCryptoStream::MockQuicCryptoStream(QuicSession* session)
    : QuicCryptoStream(session), params_(new QuicCryptoNegotiatedParameters) {}

MockQuicCryptoStream::~MockQuicCryptoStream() {}

bool MockQuicCryptoStream::encryption_established() const {
  return false;
}

bool MockQuicCryptoStream::one_rtt_keys_available() const {
  return false;
}

const QuicCryptoNegotiatedParameters&
MockQuicCryptoStream::crypto_negotiated_params() const {
  return *params_;
}

CryptoMessageParser* MockQuicCryptoStream::crypto_message_parser() {
  return &crypto_framer_;
}

MockQuicSpdySession::MockQuicSpdySession(QuicConnection* connection)
    : MockQuicSpdySession(connection, true) {}

MockQuicSpdySession::MockQuicSpdySession(QuicConnection* connection,
                                         bool create_mock_crypto_stream)
    : QuicSpdySession(connection,
                      nullptr,
                      DefaultQuicConfig(),
                      connection->supported_versions()) {
  if (create_mock_crypto_stream) {
    crypto_stream_ = std::make_unique<MockQuicCryptoStream>(this);
  }

  ON_CALL(*this, WritevData(_, _, _, _, _, _))
      .WillByDefault(testing::Return(QuicConsumedData(0, false)));
}

MockQuicSpdySession::~MockQuicSpdySession() {
  DeleteConnection();
}

QuicCryptoStream* MockQuicSpdySession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoStream* MockQuicSpdySession::GetCryptoStream() const {
  return crypto_stream_.get();
}

void MockQuicSpdySession::SetCryptoStream(QuicCryptoStream* crypto_stream) {
  crypto_stream_.reset(crypto_stream);
}

QuicConsumedData MockQuicSpdySession::ConsumeData(
    QuicStreamId id,
    size_t write_length,
    QuicStreamOffset offset,
    StreamSendingState state,
    TransmissionType /*type*/,
    quiche::QuicheOptional<EncryptionLevel> /*level*/) {
  if (write_length > 0) {
    auto buf = std::make_unique<char[]>(write_length);
    QuicStream* stream = GetOrCreateStream(id);
    DCHECK(stream);
    QuicDataWriter writer(write_length, buf.get(), quiche::HOST_BYTE_ORDER);
    stream->WriteStreamData(offset, write_length, &writer);
  } else {
    DCHECK(state != NO_FIN);
  }
  return QuicConsumedData(write_length, state != NO_FIN);
}

TestQuicSpdyServerSession::TestQuicSpdyServerSession(
    QuicConnection* connection,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache)
    : QuicServerSessionBase(config,
                            supported_versions,
                            connection,
                            &visitor_,
                            &helper_,
                            crypto_config,
                            compressed_certs_cache) {
  ON_CALL(helper_, CanAcceptClientHello(_, _, _, _, _))
      .WillByDefault(testing::Return(true));
}

TestQuicSpdyServerSession::~TestQuicSpdyServerSession() {
  DeleteConnection();
}

std::unique_ptr<QuicCryptoServerStreamBase>
TestQuicSpdyServerSession::CreateQuicCryptoServerStream(
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache) {
  return CreateCryptoServerStream(crypto_config, compressed_certs_cache, this,
                                  &helper_);
}

QuicCryptoServerStreamBase*
TestQuicSpdyServerSession::GetMutableCryptoStream() {
  return QuicServerSessionBase::GetMutableCryptoStream();
}

const QuicCryptoServerStreamBase* TestQuicSpdyServerSession::GetCryptoStream()
    const {
  return QuicServerSessionBase::GetCryptoStream();
}

TestQuicSpdyClientSession::TestQuicSpdyClientSession(
    QuicConnection* connection,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config)
    : QuicSpdyClientSessionBase(connection,
                                &push_promise_index_,
                                config,
                                supported_versions) {
  crypto_stream_ = std::make_unique<QuicCryptoClientStream>(
      server_id, this, crypto_test_utils::ProofVerifyContextForTesting(),
      crypto_config, this);
  Initialize();
}

TestQuicSpdyClientSession::~TestQuicSpdyClientSession() {}

bool TestQuicSpdyClientSession::IsAuthorized(const std::string& /*authority*/) {
  return true;
}

QuicCryptoClientStream* TestQuicSpdyClientSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoClientStream* TestQuicSpdyClientSession::GetCryptoStream()
    const {
  return crypto_stream_.get();
}

TestPushPromiseDelegate::TestPushPromiseDelegate(bool match)
    : match_(match), rendezvous_fired_(false), rendezvous_stream_(nullptr) {}

bool TestPushPromiseDelegate::CheckVary(
    const spdy::SpdyHeaderBlock& /*client_request*/,
    const spdy::SpdyHeaderBlock& /*promise_request*/,
    const spdy::SpdyHeaderBlock& /*promise_response*/) {
  QUIC_DVLOG(1) << "match " << match_;
  return match_;
}

void TestPushPromiseDelegate::OnRendezvousResult(QuicSpdyStream* stream) {
  rendezvous_fired_ = true;
  rendezvous_stream_ = stream;
}

MockPacketWriter::MockPacketWriter() {
  ON_CALL(*this, GetMaxPacketSize(_))
      .WillByDefault(testing::Return(kMaxOutgoingPacketSize));
  ON_CALL(*this, IsBatchMode()).WillByDefault(testing::Return(false));
  ON_CALL(*this, GetNextWriteLocation(_, _))
      .WillByDefault(testing::Return(nullptr));
  ON_CALL(*this, Flush())
      .WillByDefault(testing::Return(WriteResult(WRITE_STATUS_OK, 0)));
}

MockPacketWriter::~MockPacketWriter() {}

MockSendAlgorithm::MockSendAlgorithm() {
  ON_CALL(*this, PacingRate(_))
      .WillByDefault(testing::Return(QuicBandwidth::Zero()));
  ON_CALL(*this, BandwidthEstimate())
      .WillByDefault(testing::Return(QuicBandwidth::Zero()));
}

MockSendAlgorithm::~MockSendAlgorithm() {}

MockLossAlgorithm::MockLossAlgorithm() {}

MockLossAlgorithm::~MockLossAlgorithm() {}

MockAckListener::MockAckListener() {}

MockAckListener::~MockAckListener() {}

MockNetworkChangeVisitor::MockNetworkChangeVisitor() {}

MockNetworkChangeVisitor::~MockNetworkChangeVisitor() {}

QuicIpAddress TestPeerIPAddress() {
  return QuicIpAddress::Loopback4();
}

ParsedQuicVersion QuicVersionMax() {
  return AllSupportedVersions().front();
}

ParsedQuicVersion QuicVersionMin() {
  return AllSupportedVersions().back();
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data) {
  return ConstructEncryptedPacket(
      destination_connection_id, source_connection_id, version_flag, reset_flag,
      packet_number, data, CONNECTION_ID_PRESENT, CONNECTION_ID_ABSENT,
      PACKET_4BYTE_PACKET_NUMBER);
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length) {
  return ConstructEncryptedPacket(
      destination_connection_id, source_connection_id, version_flag, reset_flag,
      packet_number, data, destination_connection_id_included,
      source_connection_id_included, packet_number_length, nullptr);
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions) {
  return ConstructEncryptedPacket(
      destination_connection_id, source_connection_id, version_flag, reset_flag,
      packet_number, data, false, destination_connection_id_included,
      source_connection_id_included, packet_number_length, versions,
      Perspective::IS_CLIENT);
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data,
    bool full_padding,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions) {
  return ConstructEncryptedPacket(
      destination_connection_id, source_connection_id, version_flag, reset_flag,
      packet_number, data, full_padding, destination_connection_id_included,
      source_connection_id_included, packet_number_length, versions,
      Perspective::IS_CLIENT);
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data,
    bool full_padding,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions,
    Perspective perspective) {
  QuicPacketHeader header;
  header.destination_connection_id = destination_connection_id;
  header.destination_connection_id_included =
      destination_connection_id_included;
  header.source_connection_id = source_connection_id;
  header.source_connection_id_included = source_connection_id_included;
  header.version_flag = version_flag;
  header.reset_flag = reset_flag;
  header.packet_number_length = packet_number_length;
  header.packet_number = QuicPacketNumber(packet_number);
  ParsedQuicVersionVector supported_versions = CurrentSupportedVersions();
  if (!versions) {
    versions = &supported_versions;
  }
  EXPECT_FALSE(versions->empty());
  ParsedQuicVersion version = (*versions)[0];
  if (QuicVersionHasLongHeaderLengths(version.transport_version) &&
      version_flag) {
    header.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  QuicFrames frames;
  QuicFramer framer(*versions, QuicTime::Zero(), perspective,
                    kQuicDefaultConnectionIdLength);
  framer.SetInitialObfuscators(destination_connection_id);
  EncryptionLevel level =
      header.version_flag ? ENCRYPTION_INITIAL : ENCRYPTION_FORWARD_SECURE;
  if (level != ENCRYPTION_INITIAL) {
    framer.SetEncrypter(level, std::make_unique<NullEncrypter>(perspective));
  }
  if (!QuicVersionUsesCryptoFrames(version.transport_version)) {
    QuicFrame frame(
        QuicStreamFrame(QuicUtils::GetCryptoStreamId(version.transport_version),
                        false, 0, quiche::QuicheStringPiece(data)));
    frames.push_back(frame);
  } else {
    QuicFrame frame(new QuicCryptoFrame(level, 0, data));
    frames.push_back(frame);
  }
  if (full_padding) {
    frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
  } else {
    // We need a minimum number of bytes of encrypted payload. This will
    // guarantee that we have at least that much. (It ignores the overhead of
    // the stream/crypto framing, so it overpads slightly.)
    size_t min_plaintext_size =
        QuicPacketCreator::MinPlaintextPacketSize(version);
    if (data.length() < min_plaintext_size) {
      size_t padding_length = min_plaintext_size - data.length();
      frames.push_back(QuicFrame(QuicPaddingFrame(padding_length)));
    }
  }

  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  EXPECT_TRUE(packet != nullptr);
  char* buffer = new char[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      framer.EncryptPayload(level, QuicPacketNumber(packet_number), *packet,
                            buffer, kMaxOutgoingPacketSize);
  EXPECT_NE(0u, encrypted_length);
  DeleteFrames(&frames);
  return new QuicEncryptedPacket(buffer, encrypted_length, true);
}

QuicReceivedPacket* ConstructReceivedPacket(
    const QuicEncryptedPacket& encrypted_packet,
    QuicTime receipt_time) {
  char* buffer = new char[encrypted_packet.length()];
  memcpy(buffer, encrypted_packet.data(), encrypted_packet.length());
  return new QuicReceivedPacket(buffer, encrypted_packet.length(), receipt_time,
                                true);
}

QuicEncryptedPacket* ConstructMisFramedEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersion version,
    Perspective perspective) {
  QuicPacketHeader header;
  header.destination_connection_id = destination_connection_id;
  header.destination_connection_id_included =
      destination_connection_id_included;
  header.source_connection_id = source_connection_id;
  header.source_connection_id_included = source_connection_id_included;
  header.version_flag = version_flag;
  header.reset_flag = reset_flag;
  header.packet_number_length = packet_number_length;
  header.packet_number = QuicPacketNumber(packet_number);
  if (QuicVersionHasLongHeaderLengths(version.transport_version) &&
      version_flag) {
    header.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
  QuicFrame frame(
      QuicStreamFrame(1, false, 0, quiche::QuicheStringPiece(data)));
  QuicFrames frames;
  frames.push_back(frame);
  QuicFramer framer({version}, QuicTime::Zero(), perspective,
                    kQuicDefaultConnectionIdLength);
  framer.SetInitialObfuscators(destination_connection_id);
  EncryptionLevel level =
      version_flag ? ENCRYPTION_INITIAL : ENCRYPTION_FORWARD_SECURE;
  if (level != ENCRYPTION_INITIAL) {
    framer.SetEncrypter(level, std::make_unique<NullEncrypter>(perspective));
  }
  // We need a minimum of 7 bytes of encrypted payload. This will guarantee that
  // we have at least that much. (It ignores the overhead of the stream/crypto
  // framing, so it overpads slightly.)
  if (data.length() < 7) {
    size_t padding_length = 7 - data.length();
    frames.push_back(QuicFrame(QuicPaddingFrame(padding_length)));
  }

  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  EXPECT_TRUE(packet != nullptr);

  // Now set the frame type to 0x1F, which is an invalid frame type.
  reinterpret_cast<unsigned char*>(
      packet->mutable_data())[GetStartOfEncryptedData(
      framer.transport_version(),
      GetIncludedDestinationConnectionIdLength(header),
      GetIncludedSourceConnectionIdLength(header), version_flag,
      false /* no diversification nonce */, packet_number_length,
      header.retry_token_length_length, 0, header.length_length)] = 0x1F;

  char* buffer = new char[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      framer.EncryptPayload(level, QuicPacketNumber(packet_number), *packet,
                            buffer, kMaxOutgoingPacketSize);
  EXPECT_NE(0u, encrypted_length);
  return new QuicEncryptedPacket(buffer, encrypted_length, true);
}

QuicConfig DefaultQuicConfig() {
  QuicConfig config;
  config.SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
      kInitialStreamFlowControlWindowForTest);
  config.SetInitialMaxStreamDataBytesOutgoingBidirectionalToSend(
      kInitialStreamFlowControlWindowForTest);
  config.SetInitialMaxStreamDataBytesUnidirectionalToSend(
      kInitialStreamFlowControlWindowForTest);
  config.SetInitialStreamFlowControlWindowToSend(
      kInitialStreamFlowControlWindowForTest);
  config.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest);
  QuicConfigPeer::SetReceivedMaxBidirectionalStreams(
      &config, kDefaultMaxStreamsPerConnection);
  // Default enable NSTP.
  // This is unnecessary for versions > 44
  if (!config.HasClientSentConnectionOption(quic::kNSTP,
                                            quic::Perspective::IS_CLIENT)) {
    quic::QuicTagVector connection_options;
    connection_options.push_back(quic::kNSTP);
    config.SetConnectionOptionsToSend(connection_options);
  }
  return config;
}

ParsedQuicVersionVector SupportedVersions(ParsedQuicVersion version) {
  ParsedQuicVersionVector versions;
  versions.push_back(version);
  return versions;
}

MockQuicConnectionDebugVisitor::MockQuicConnectionDebugVisitor() {}

MockQuicConnectionDebugVisitor::~MockQuicConnectionDebugVisitor() {}

MockReceivedPacketManager::MockReceivedPacketManager(QuicConnectionStats* stats)
    : QuicReceivedPacketManager(stats) {}

MockReceivedPacketManager::~MockReceivedPacketManager() {}

MockPacketCreatorDelegate::MockPacketCreatorDelegate() {}
MockPacketCreatorDelegate::~MockPacketCreatorDelegate() {}

MockSessionNotifier::MockSessionNotifier() {}
MockSessionNotifier::~MockSessionNotifier() {}

void CreateClientSessionForTest(
    QuicServerId server_id,
    QuicTime::Delta connection_start_time,
    const ParsedQuicVersionVector& supported_versions,
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
    QuicCryptoClientConfig* crypto_client_config,
    PacketSavingConnection** client_connection,
    TestQuicSpdyClientSession** client_session) {
  CHECK(crypto_client_config);
  CHECK(client_connection);
  CHECK(client_session);
  CHECK(!connection_start_time.IsZero())
      << "Connections must start at non-zero times, otherwise the "
      << "strike-register will be unhappy.";

  QuicConfig config = DefaultQuicConfig();
  *client_connection = new PacketSavingConnection(
      helper, alarm_factory, Perspective::IS_CLIENT, supported_versions);
  *client_session = new TestQuicSpdyClientSession(*client_connection, config,
                                                  supported_versions, server_id,
                                                  crypto_client_config);
  (*client_connection)->AdvanceTime(connection_start_time);
}

void CreateServerSessionForTest(
    QuicServerId /*server_id*/,
    QuicTime::Delta connection_start_time,
    ParsedQuicVersionVector supported_versions,
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
    QuicCryptoServerConfig* server_crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    PacketSavingConnection** server_connection,
    TestQuicSpdyServerSession** server_session) {
  CHECK(server_crypto_config);
  CHECK(server_connection);
  CHECK(server_session);
  CHECK(!connection_start_time.IsZero())
      << "Connections must start at non-zero times, otherwise the "
      << "strike-register will be unhappy.";

  *server_connection =
      new PacketSavingConnection(helper, alarm_factory, Perspective::IS_SERVER,
                                 ParsedVersionOfIndex(supported_versions, 0));
  *server_session = new TestQuicSpdyServerSession(
      *server_connection, DefaultQuicConfig(), supported_versions,
      server_crypto_config, compressed_certs_cache);
  (*server_session)->Initialize();

  // We advance the clock initially because the default time is zero and the
  // strike register worries that we've just overflowed a uint32_t time.
  (*server_connection)->AdvanceTime(connection_start_time);
}

QuicStreamId GetNthClientInitiatedBidirectionalStreamId(
    QuicTransportVersion version,
    int n) {
  int num = n;
  if (!VersionUsesHttp3(version)) {
    num++;
  }
  return QuicUtils::GetFirstBidirectionalStreamId(version,
                                                  Perspective::IS_CLIENT) +
         QuicUtils::StreamIdDelta(version) * num;
}

QuicStreamId GetNthServerInitiatedBidirectionalStreamId(
    QuicTransportVersion version,
    int n) {
  return QuicUtils::GetFirstBidirectionalStreamId(version,
                                                  Perspective::IS_SERVER) +
         QuicUtils::StreamIdDelta(version) * n;
}

QuicStreamId GetNthServerInitiatedUnidirectionalStreamId(
    QuicTransportVersion version,
    int n) {
  return QuicUtils::GetFirstUnidirectionalStreamId(version,
                                                   Perspective::IS_SERVER) +
         QuicUtils::StreamIdDelta(version) * n;
}

QuicStreamId GetNthClientInitiatedUnidirectionalStreamId(
    QuicTransportVersion version,
    int n) {
  return QuicUtils::GetFirstUnidirectionalStreamId(version,
                                                   Perspective::IS_CLIENT) +
         QuicUtils::StreamIdDelta(version) * n;
}

StreamType DetermineStreamType(QuicStreamId id,
                               QuicTransportVersion version,
                               Perspective perspective,
                               bool is_incoming,
                               StreamType default_type) {
  return VersionHasIetfQuicFrames(version)
             ? QuicUtils::GetStreamType(id, perspective, is_incoming)
             : default_type;
}

QuicMemSliceSpan MakeSpan(QuicBufferAllocator* allocator,
                          quiche::QuicheStringPiece message_data,
                          QuicMemSliceStorage* storage) {
  if (message_data.length() == 0) {
    *storage =
        QuicMemSliceStorage(nullptr, 0, allocator, kMaxOutgoingPacketSize);
    return storage->ToSpan();
  }
  struct iovec iov = {const_cast<char*>(message_data.data()),
                      message_data.length()};
  *storage = QuicMemSliceStorage(&iov, 1, allocator, kMaxOutgoingPacketSize);
  return storage->ToSpan();
}

QuicMemSlice MemSliceFromString(quiche::QuicheStringPiece data) {
  static SimpleBufferAllocator* allocator = new SimpleBufferAllocator();
  QuicUniqueBufferPtr buffer = MakeUniqueBuffer(allocator, data.size());
  memcpy(buffer.get(), data.data(), data.size());
  return QuicMemSlice(std::move(buffer), data.size());
}

}  // namespace test
}  // namespace quic
