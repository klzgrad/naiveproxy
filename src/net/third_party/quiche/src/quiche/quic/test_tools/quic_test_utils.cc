// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_test_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "openssl/chacha.h"
#include "openssl/sha.h"
#include "quiche/quic/core/crypto/crypto_framer.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/crypto/null_decrypter.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_config_peer.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/spdy/core/spdy_frame_builder.h"

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
  QUICHE_DCHECK_EQ(connection_id.length(), kQuicDefaultConnectionIdLength);
  uint64_t connection_id64_net = 0;
  memcpy(&connection_id64_net, connection_id.data(),
         std::min<size_t>(static_cast<size_t>(connection_id.length()),
                          sizeof(connection_id64_net)));
  return quiche::QuicheEndian::NetToHost64(connection_id64_net);
}

std::vector<uint8_t> CreateStatelessResetTokenForTest() {
  static constexpr uint8_t kStatelessResetTokenDataForTest[16] = {
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};
  return std::vector<uint8_t>(kStatelessResetTokenDataForTest,
                              kStatelessResetTokenDataForTest +
                                  sizeof(kStatelessResetTokenDataForTest));
}

std::string TestHostname() { return "test.example.com"; }

QuicServerId TestServerId() { return QuicServerId(TestHostname(), kTestPort); }

QuicAckFrame InitAckFrame(const std::vector<QuicAckBlock>& ack_blocks) {
  QUICHE_DCHECK_GT(ack_blocks.size(), 0u);

  QuicAckFrame ack;
  QuicPacketNumber end_of_previous_block(1);
  for (const QuicAckBlock& block : ack_blocks) {
    QUICHE_DCHECK_GE(block.start, end_of_previous_block);
    QUICHE_DCHECK_GT(block.limit, block.start);
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

QuicAckFrame MakeAckFrameWithGaps(uint64_t gap_size, size_t max_num_gaps,
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
    QuicFramer* framer, const QuicPacketHeader& header,
    const QuicFrames& frames) {
  const size_t max_plaintext_size =
      framer->GetMaxPlaintextSize(kMaxOutgoingPacketSize);
  size_t packet_size = GetPacketHeaderSize(framer->transport_version(), header);
  for (size_t i = 0; i < frames.size(); ++i) {
    QUICHE_DCHECK_LE(packet_size, max_plaintext_size);
    bool first_frame = i == 0;
    bool last_frame = i == frames.size() - 1;
    const size_t frame_size = framer->GetSerializedFrameLength(
        frames[i], max_plaintext_size - packet_size, first_frame, last_frame,
        header.packet_number_length);
    QUICHE_DCHECK(frame_size);
    packet_size += frame_size;
  }
  return BuildUnsizedDataPacket(framer, header, frames, packet_size);
}

std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer, const QuicPacketHeader& header,
    const QuicFrames& frames, size_t packet_size) {
  char* buffer = new char[packet_size];
  EncryptionLevel level = HeaderToEncryptionLevel(header);
  size_t length =
      framer->BuildDataPacket(header, frames, buffer, packet_size, level);

  if (length == 0) {
    delete[] buffer;
    return nullptr;
  }
  // Re-construct the data packet with data ownership.
  return std::make_unique<QuicPacket>(
      buffer, length, /* owns_buffer */ true,
      GetIncludedDestinationConnectionIdLength(header),
      GetIncludedSourceConnectionIdLength(header), header.version_flag,
      header.nonce != nullptr, header.packet_number_length,
      header.retry_token_length_length, header.retry_token.length(),
      header.length_length);
}

std::string Sha1Hash(absl::string_view data) {
  char buffer[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
       reinterpret_cast<uint8_t*>(buffer));
  return std::string(buffer, ABSL_ARRAYSIZE(buffer));
}

bool ClearControlFrame(const QuicFrame& frame) {
  DeleteFrame(&const_cast<QuicFrame&>(frame));
  return true;
}

bool ClearControlFrameWithTransmissionType(const QuicFrame& frame,
                                           TransmissionType /*type*/) {
  return ClearControlFrame(frame);
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

void SimpleRandom::InsecureRandBytes(void* data, size_t len) {
  RandBytes(data, len);
}

uint64_t SimpleRandom::InsecureRandUint64() { return RandUint64(); }

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
    const QuicEncryptedPacket& /*packet*/, EncryptionLevel /*decryption_level*/,
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

bool NoOpFramerVisitor::OnAckFrameEnd(
    QuicPacketNumber /*start*/,
    const absl::optional<QuicEcnCounts>& /*ecn_counts*/) {
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

bool NoOpFramerVisitor::OnAckFrequencyFrame(
    const QuicAckFrequencyFrame& /*frame*/) {
  return true;
}

bool NoOpFramerVisitor::IsValidStatelessResetToken(
    const StatelessResetToken& /*token*/) const {
  return false;
}

MockQuicConnectionVisitor::MockQuicConnectionVisitor() {}

MockQuicConnectionVisitor::~MockQuicConnectionVisitor() {}

MockQuicConnectionHelper::MockQuicConnectionHelper() {}

MockQuicConnectionHelper::~MockQuicConnectionHelper() {}

const MockClock* MockQuicConnectionHelper::GetClock() const { return &clock_; }

MockClock* MockQuicConnectionHelper::GetClock() { return &clock_; }

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

quiche::QuicheBufferAllocator*
MockQuicConnectionHelper::GetStreamSendBufferAllocator() {
  return &buffer_allocator_;
}

void MockQuicConnectionHelper::AdvanceTime(QuicTime::Delta delta) {
  clock_.AdvanceTime(delta);
}

MockQuicConnection::MockQuicConnection(QuicConnectionHelperInterface* helper,
                                       QuicAlarmFactory* alarm_factory,
                                       Perspective perspective)
    : MockQuicConnection(TestConnectionId(),
                         QuicSocketAddress(TestPeerIPAddress(), kTestPort),
                         helper, alarm_factory, perspective,
                         ParsedVersionOfIndex(CurrentSupportedVersions(), 0)) {}

MockQuicConnection::MockQuicConnection(QuicSocketAddress address,
                                       QuicConnectionHelperInterface* helper,
                                       QuicAlarmFactory* alarm_factory,
                                       Perspective perspective)
    : MockQuicConnection(TestConnectionId(), address, helper, alarm_factory,
                         perspective,
                         ParsedVersionOfIndex(CurrentSupportedVersions(), 0)) {}

MockQuicConnection::MockQuicConnection(QuicConnectionId connection_id,
                                       QuicConnectionHelperInterface* helper,
                                       QuicAlarmFactory* alarm_factory,
                                       Perspective perspective)
    : MockQuicConnection(connection_id,
                         QuicSocketAddress(TestPeerIPAddress(), kTestPort),
                         helper, alarm_factory, perspective,
                         ParsedVersionOfIndex(CurrentSupportedVersions(), 0)) {}

MockQuicConnection::MockQuicConnection(
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    Perspective perspective, const ParsedQuicVersionVector& supported_versions)
    : MockQuicConnection(
          TestConnectionId(), QuicSocketAddress(TestPeerIPAddress(), kTestPort),
          helper, alarm_factory, perspective, supported_versions) {}

MockQuicConnection::MockQuicConnection(
    QuicConnectionId connection_id, QuicSocketAddress initial_peer_address,
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    Perspective perspective, const ParsedQuicVersionVector& supported_versions)
    : QuicConnection(
          connection_id,
          /*initial_self_address=*/QuicSocketAddress(QuicIpAddress::Any4(), 5),
          initial_peer_address, helper, alarm_factory,
          new testing::NiceMock<MockPacketWriter>(),
          /* owns_writer= */ true, perspective, supported_versions,
          connection_id_generator_) {
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
                                               QuicAlarmFactory* alarm_factory,
                                               Perspective perspective)
    : MockQuicConnection(helper, alarm_factory, perspective),
      mock_helper_(helper) {}

PacketSavingConnection::PacketSavingConnection(
    MockQuicConnectionHelper* helper, QuicAlarmFactory* alarm_factory,
    Perspective perspective, const ParsedQuicVersionVector& supported_versions)
    : MockQuicConnection(helper, alarm_factory, perspective,
                         supported_versions),
      mock_helper_(helper) {}

PacketSavingConnection::~PacketSavingConnection() {}

SerializedPacketFate PacketSavingConnection::GetSerializedPacketFate(
    bool /*is_mtu_discovery*/, EncryptionLevel /*encryption_level*/) {
  return SEND_TO_WRITER;
}

void PacketSavingConnection::SendOrQueuePacket(SerializedPacket packet) {
  encrypted_packets_.push_back(std::make_unique<QuicEncryptedPacket>(
      CopyBuffer(packet), packet.encrypted_length, true));
  MockClock& clock = *mock_helper_->GetClock();
  clock.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  // Transfer ownership of the packet to the SentPacketManager and the
  // ack notifier to the AckNotifierManager.
  OnPacketSent(packet.encryption_level, packet.transmission_type);
  QuicConnectionPeer::GetSentPacketManager(this)->OnPacketSent(
      &packet, clock.ApproximateNow(), NOT_RETRANSMISSION,
      HAS_RETRANSMITTABLE_DATA, true, ECN_NOT_ECT);
}

std::vector<const QuicEncryptedPacket*> PacketSavingConnection::GetPackets()
    const {
  std::vector<const QuicEncryptedPacket*> packets;
  for (size_t i = num_cleared_packets_; i < encrypted_packets_.size(); ++i) {
    packets.push_back(encrypted_packets_[i].get());
  }
  return packets;
}

void PacketSavingConnection::ClearPackets() {
  num_cleared_packets_ = encrypted_packets_.size();
}

MockQuicSession::MockQuicSession(QuicConnection* connection)
    : MockQuicSession(connection, true) {}

MockQuicSession::MockQuicSession(QuicConnection* connection,
                                 bool create_mock_crypto_stream)
    : QuicSession(connection, nullptr, DefaultQuicConfig(),
                  connection->supported_versions(),
                  /*num_expected_unidirectional_static_streams = */ 0) {
  if (create_mock_crypto_stream) {
    crypto_stream_ =
        std::make_unique<testing::NiceMock<MockQuicCryptoStream>>(this);
  }
  ON_CALL(*this, WritevData(_, _, _, _, _, _))
      .WillByDefault(testing::Return(QuicConsumedData(0, false)));
}

MockQuicSession::~MockQuicSession() { DeleteConnection(); }

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
    QuicStreamId id, size_t write_length, QuicStreamOffset offset,
    StreamSendingState state, TransmissionType /*type*/,
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

MockQuicCryptoStream::MockQuicCryptoStream(QuicSession* session)
    : QuicCryptoStream(session), params_(new QuicCryptoNegotiatedParameters) {}

MockQuicCryptoStream::~MockQuicCryptoStream() {}

ssl_early_data_reason_t MockQuicCryptoStream::EarlyDataReason() const {
  return ssl_early_data_unknown;
}

bool MockQuicCryptoStream::one_rtt_keys_available() const { return false; }

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
    : QuicSpdySession(connection, nullptr, DefaultQuicConfig(),
                      connection->supported_versions()) {
  if (create_mock_crypto_stream) {
    crypto_stream_ = std::make_unique<MockQuicCryptoStream>(this);
  }

  ON_CALL(*this, WritevData(_, _, _, _, _, _))
      .WillByDefault(testing::Return(QuicConsumedData(0, false)));

  ON_CALL(*this, SendWindowUpdate(_, _))
      .WillByDefault([this](QuicStreamId id, QuicStreamOffset byte_offset) {
        return QuicSpdySession::SendWindowUpdate(id, byte_offset);
      });

  ON_CALL(*this, SendBlocked(_, _))
      .WillByDefault([this](QuicStreamId id, QuicStreamOffset byte_offset) {
        return QuicSpdySession::SendBlocked(id, byte_offset);
      });

  ON_CALL(*this, OnCongestionWindowChange(_)).WillByDefault(testing::Return());
}

MockQuicSpdySession::~MockQuicSpdySession() { DeleteConnection(); }

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
    QuicStreamId id, size_t write_length, QuicStreamOffset offset,
    StreamSendingState state, TransmissionType /*type*/,
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

TestQuicSpdyServerSession::TestQuicSpdyServerSession(
    QuicConnection* connection, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache)
    : QuicServerSessionBase(config, supported_versions, connection, &visitor_,
                            &helper_, crypto_config, compressed_certs_cache) {
  ON_CALL(helper_, CanAcceptClientHello(_, _, _, _, _))
      .WillByDefault(testing::Return(true));
}

TestQuicSpdyServerSession::~TestQuicSpdyServerSession() { DeleteConnection(); }

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
    QuicConnection* connection, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicServerId& server_id, QuicCryptoClientConfig* crypto_config,
    absl::optional<QuicSSLConfig> ssl_config)
    : QuicSpdyClientSessionBase(connection, nullptr, config,
                                supported_versions),
      ssl_config_(std::move(ssl_config)) {
  // TODO(b/153726130): Consider adding SetServerApplicationStateForResumption
  // calls in tests and set |has_application_state| to true.
  crypto_stream_ = std::make_unique<QuicCryptoClientStream>(
      server_id, this, crypto_test_utils::ProofVerifyContextForTesting(),
      crypto_config, this, /*has_application_state = */ false);
  Initialize();
  ON_CALL(*this, OnConfigNegotiated())
      .WillByDefault(
          Invoke(this, &TestQuicSpdyClientSession::RealOnConfigNegotiated));
}

TestQuicSpdyClientSession::~TestQuicSpdyClientSession() {}

QuicCryptoClientStream* TestQuicSpdyClientSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoClientStream* TestQuicSpdyClientSession::GetCryptoStream()
    const {
  return crypto_stream_.get();
}

void TestQuicSpdyClientSession::RealOnConfigNegotiated() {
  QuicSpdyClientSessionBase::OnConfigNegotiated();
}

MockPacketWriter::MockPacketWriter() {
  ON_CALL(*this, GetMaxPacketSize(_))
      .WillByDefault(testing::Return(kMaxOutgoingPacketSize));
  ON_CALL(*this, IsBatchMode()).WillByDefault(testing::Return(false));
  ON_CALL(*this, GetNextWriteLocation(_, _))
      .WillByDefault(testing::Return(QuicPacketBuffer()));
  ON_CALL(*this, Flush())
      .WillByDefault(testing::Return(WriteResult(WRITE_STATUS_OK, 0)));
  ON_CALL(*this, SupportsReleaseTime()).WillByDefault(testing::Return(false));
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

QuicIpAddress TestPeerIPAddress() { return QuicIpAddress::Loopback4(); }

ParsedQuicVersion QuicVersionMax() { return AllSupportedVersions().front(); }

ParsedQuicVersion QuicVersionMin() { return AllSupportedVersions().back(); }

void DisableQuicVersionsWithTls() {
  for (const ParsedQuicVersion& version : AllSupportedVersionsWithTls()) {
    QuicDisableVersion(version);
  }
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data) {
  return ConstructEncryptedPacket(
      destination_connection_id, source_connection_id, version_flag, reset_flag,
      packet_number, data, CONNECTION_ID_PRESENT, CONNECTION_ID_ABSENT,
      PACKET_4BYTE_PACKET_NUMBER);
}

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data,
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
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data,
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
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data, bool full_padding,
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
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data, bool full_padding,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions, Perspective perspective) {
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
    header.retry_token_length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  QuicFrames frames;
  QuicFramer framer(*versions, QuicTime::Zero(), perspective,
                    kQuicDefaultConnectionIdLength);
  framer.SetInitialObfuscators(destination_connection_id);
  EncryptionLevel level =
      header.version_flag ? ENCRYPTION_INITIAL : ENCRYPTION_FORWARD_SECURE;
  if (level != ENCRYPTION_INITIAL) {
    framer.SetEncrypter(level, std::make_unique<TaggingEncrypter>(level));
  }
  if (!QuicVersionUsesCryptoFrames(version.transport_version)) {
    QuicFrame frame(
        QuicStreamFrame(QuicUtils::GetCryptoStreamId(version.transport_version),
                        false, 0, absl::string_view(data)));
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
    size_t min_plaintext_size = QuicPacketCreator::MinPlaintextPacketSize(
        version, packet_number_length);
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

std::unique_ptr<QuicEncryptedPacket> GetUndecryptableEarlyPacket(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id) {
  QuicPacketHeader header;
  header.destination_connection_id = server_connection_id;
  header.destination_connection_id_included = CONNECTION_ID_PRESENT;
  header.source_connection_id = EmptyQuicConnectionId();
  header.source_connection_id_included = CONNECTION_ID_PRESENT;
  if (!version.SupportsClientConnectionIds()) {
    header.source_connection_id_included = CONNECTION_ID_ABSENT;
  }
  header.version_flag = true;
  header.reset_flag = false;
  header.packet_number_length = PACKET_4BYTE_PACKET_NUMBER;
  header.packet_number = QuicPacketNumber(33);
  header.long_packet_type = ZERO_RTT_PROTECTED;
  if (version.HasLongHeaderLengths()) {
    header.retry_token_length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  QuicFrames frames;
  frames.push_back(QuicFrame(QuicPingFrame()));
  frames.push_back(QuicFrame(QuicPaddingFrame(100)));
  QuicFramer framer({version}, QuicTime::Zero(), Perspective::IS_CLIENT,
                    kQuicDefaultConnectionIdLength);
  framer.SetInitialObfuscators(server_connection_id);

  framer.SetEncrypter(ENCRYPTION_ZERO_RTT,
                      std::make_unique<TaggingEncrypter>(ENCRYPTION_ZERO_RTT));
  std::unique_ptr<QuicPacket> packet(
      BuildUnsizedDataPacket(&framer, header, frames));
  EXPECT_TRUE(packet != nullptr);
  char* buffer = new char[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      framer.EncryptPayload(ENCRYPTION_ZERO_RTT, header.packet_number, *packet,
                            buffer, kMaxOutgoingPacketSize);
  EXPECT_NE(0u, encrypted_length);
  DeleteFrames(&frames);
  return std::make_unique<QuicEncryptedPacket>(buffer, encrypted_length,
                                               /*owns_buffer=*/true);
}

QuicReceivedPacket* ConstructReceivedPacket(
    const QuicEncryptedPacket& encrypted_packet, QuicTime receipt_time) {
  char* buffer = new char[encrypted_packet.length()];
  memcpy(buffer, encrypted_packet.data(), encrypted_packet.length());
  return new QuicReceivedPacket(buffer, encrypted_packet.length(), receipt_time,
                                true);
}

QuicEncryptedPacket* ConstructMisFramedEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length, ParsedQuicVersion version,
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
    header.retry_token_length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
  QuicFrame frame(QuicStreamFrame(1, false, 0, absl::string_view(data)));
  QuicFrames frames;
  frames.push_back(frame);
  QuicFramer framer({version}, QuicTime::Zero(), perspective,
                    kQuicDefaultConnectionIdLength);
  framer.SetInitialObfuscators(destination_connection_id);
  EncryptionLevel level =
      version_flag ? ENCRYPTION_INITIAL : ENCRYPTION_FORWARD_SECURE;
  if (level != ENCRYPTION_INITIAL) {
    framer.SetEncrypter(level, std::make_unique<TaggingEncrypter>(level));
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

// static
QuicCryptoClientStream::HandshakerInterface*
QuicCryptoClientStreamPeer::GetHandshaker(QuicCryptoClientStream* stream) {
  return stream->handshaker_.get();
}

void CreateClientSessionForTest(
    QuicServerId server_id, QuicTime::Delta connection_start_time,
    const ParsedQuicVersionVector& supported_versions,
    MockQuicConnectionHelper* helper, QuicAlarmFactory* alarm_factory,
    QuicCryptoClientConfig* crypto_client_config,
    PacketSavingConnection** client_connection,
    TestQuicSpdyClientSession** client_session) {
  QUICHE_CHECK(crypto_client_config);
  QUICHE_CHECK(client_connection);
  QUICHE_CHECK(client_session);
  QUICHE_CHECK(!connection_start_time.IsZero())
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
    QuicServerId /*server_id*/, QuicTime::Delta connection_start_time,
    ParsedQuicVersionVector supported_versions,
    MockQuicConnectionHelper* helper, QuicAlarmFactory* alarm_factory,
    QuicCryptoServerConfig* server_crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    PacketSavingConnection** server_connection,
    TestQuicSpdyServerSession** server_session) {
  QUICHE_CHECK(server_crypto_config);
  QUICHE_CHECK(server_connection);
  QUICHE_CHECK(server_session);
  QUICHE_CHECK(!connection_start_time.IsZero())
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
    QuicTransportVersion version, int n) {
  int num = n;
  if (!VersionUsesHttp3(version)) {
    num++;
  }
  return QuicUtils::GetFirstBidirectionalStreamId(version,
                                                  Perspective::IS_CLIENT) +
         QuicUtils::StreamIdDelta(version) * num;
}

QuicStreamId GetNthServerInitiatedBidirectionalStreamId(
    QuicTransportVersion version, int n) {
  return QuicUtils::GetFirstBidirectionalStreamId(version,
                                                  Perspective::IS_SERVER) +
         QuicUtils::StreamIdDelta(version) * n;
}

QuicStreamId GetNthServerInitiatedUnidirectionalStreamId(
    QuicTransportVersion version, int n) {
  return QuicUtils::GetFirstUnidirectionalStreamId(version,
                                                   Perspective::IS_SERVER) +
         QuicUtils::StreamIdDelta(version) * n;
}

QuicStreamId GetNthClientInitiatedUnidirectionalStreamId(
    QuicTransportVersion version, int n) {
  return QuicUtils::GetFirstUnidirectionalStreamId(version,
                                                   Perspective::IS_CLIENT) +
         QuicUtils::StreamIdDelta(version) * n;
}

StreamType DetermineStreamType(QuicStreamId id, ParsedQuicVersion version,
                               Perspective perspective, bool is_incoming,
                               StreamType default_type) {
  return version.HasIetfQuicFrames()
             ? QuicUtils::GetStreamType(id, perspective, is_incoming, version)
             : default_type;
}

quiche::QuicheMemSlice MemSliceFromString(absl::string_view data) {
  if (data.empty()) {
    return quiche::QuicheMemSlice();
  }

  static quiche::SimpleBufferAllocator* allocator =
      new quiche::SimpleBufferAllocator();
  return quiche::QuicheMemSlice(quiche::QuicheBuffer::Copy(allocator, data));
}

bool TaggingEncrypter::EncryptPacket(uint64_t /*packet_number*/,
                                     absl::string_view /*associated_data*/,
                                     absl::string_view plaintext, char* output,
                                     size_t* output_length,
                                     size_t max_output_length) {
  const size_t len = plaintext.size() + kTagSize;
  if (max_output_length < len) {
    return false;
  }
  // Memmove is safe for inplace encryption.
  memmove(output, plaintext.data(), plaintext.size());
  output += plaintext.size();
  memset(output, tag_, kTagSize);
  *output_length = len;
  return true;
}

bool TaggingDecrypter::DecryptPacket(uint64_t /*packet_number*/,
                                     absl::string_view /*associated_data*/,
                                     absl::string_view ciphertext, char* output,
                                     size_t* output_length,
                                     size_t /*max_output_length*/) {
  if (ciphertext.size() < kTagSize) {
    return false;
  }
  if (!CheckTag(ciphertext, GetTag(ciphertext))) {
    return false;
  }
  *output_length = ciphertext.size() - kTagSize;
  memcpy(output, ciphertext.data(), *output_length);
  return true;
}

bool TaggingDecrypter::CheckTag(absl::string_view ciphertext, uint8_t tag) {
  for (size_t i = ciphertext.size() - kTagSize; i < ciphertext.size(); i++) {
    if (ciphertext.data()[i] != tag) {
      return false;
    }
  }

  return true;
}

TestPacketWriter::TestPacketWriter(ParsedQuicVersion version, MockClock* clock,
                                   Perspective perspective)
    : version_(version),
      framer_(SupportedVersions(version_),
              QuicUtils::InvertPerspective(perspective)),
      clock_(clock) {
  QuicFramerPeer::SetLastSerializedServerConnectionId(framer_.framer(),
                                                      TestConnectionId());
  framer_.framer()->SetInitialObfuscators(TestConnectionId());

  for (int i = 0; i < 128; ++i) {
    PacketBuffer* p = new PacketBuffer();
    packet_buffer_pool_.push_back(p);
    packet_buffer_pool_index_[p->buffer] = p;
    packet_buffer_free_list_.push_back(p);
  }
}

TestPacketWriter::~TestPacketWriter() {
  EXPECT_EQ(packet_buffer_pool_.size(), packet_buffer_free_list_.size())
      << packet_buffer_pool_.size() - packet_buffer_free_list_.size()
      << " out of " << packet_buffer_pool_.size()
      << " packet buffers have been leaked.";
  for (auto p : packet_buffer_pool_) {
    delete p;
  }
}

WriteResult TestPacketWriter::WritePacket(
    const char* buffer, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address, PerPacketOptions* /*options*/,
    const QuicPacketWriterParams& params) {
  last_write_source_address_ = self_address;
  last_write_peer_address_ = peer_address;
  // If the buffer is allocated from the pool, return it back to the pool.
  // Note the buffer content doesn't change.
  if (packet_buffer_pool_index_.find(const_cast<char*>(buffer)) !=
      packet_buffer_pool_index_.end()) {
    FreePacketBuffer(buffer);
  }

  QuicEncryptedPacket packet(buffer, buf_len);
  ++packets_write_attempts_;

  if (packet.length() >= sizeof(final_bytes_of_last_packet_)) {
    final_bytes_of_previous_packet_ = final_bytes_of_last_packet_;
    memcpy(&final_bytes_of_last_packet_, packet.data() + packet.length() - 4,
           sizeof(final_bytes_of_last_packet_));
  }
  if (framer_.framer()->version().KnowsWhichDecrypterToUse()) {
    framer_.framer()->InstallDecrypter(ENCRYPTION_HANDSHAKE,
                                       std::make_unique<TaggingDecrypter>());
    framer_.framer()->InstallDecrypter(ENCRYPTION_ZERO_RTT,
                                       std::make_unique<TaggingDecrypter>());
    framer_.framer()->InstallDecrypter(ENCRYPTION_FORWARD_SECURE,
                                       std::make_unique<TaggingDecrypter>());
  } else if (!framer_.framer()->HasDecrypterOfEncryptionLevel(
                 ENCRYPTION_FORWARD_SECURE) &&
             !framer_.framer()->HasDecrypterOfEncryptionLevel(
                 ENCRYPTION_ZERO_RTT)) {
    framer_.framer()->SetAlternativeDecrypter(
        ENCRYPTION_FORWARD_SECURE,
        std::make_unique<StrictTaggingDecrypter>(ENCRYPTION_FORWARD_SECURE),
        false);
  }
  EXPECT_EQ(next_packet_processable_, framer_.ProcessPacket(packet))
      << framer_.framer()->detailed_error() << " perspective "
      << framer_.framer()->perspective();
  next_packet_processable_ = true;
  if (block_on_next_write_) {
    write_blocked_ = true;
    block_on_next_write_ = false;
  }
  if (next_packet_too_large_) {
    next_packet_too_large_ = false;
    return WriteResult(WRITE_STATUS_ERROR, *MessageTooBigErrorCode());
  }
  if (always_get_packet_too_large_) {
    return WriteResult(WRITE_STATUS_ERROR, *MessageTooBigErrorCode());
  }
  if (IsWriteBlocked()) {
    return WriteResult(is_write_blocked_data_buffered_
                           ? WRITE_STATUS_BLOCKED_DATA_BUFFERED
                           : WRITE_STATUS_BLOCKED,
                       0);
  }

  if (ShouldWriteFail()) {
    return WriteResult(WRITE_STATUS_ERROR, write_error_code_);
  }

  last_packet_size_ = packet.length();
  total_bytes_written_ += packet.length();
  last_packet_header_ = framer_.header();
  if (!framer_.connection_close_frames().empty()) {
    ++connection_close_packets_;
  }
  if (!write_pause_time_delta_.IsZero()) {
    clock_->AdvanceTime(write_pause_time_delta_);
  }
  if (is_batch_mode_) {
    bytes_buffered_ += last_packet_size_;
    return WriteResult(WRITE_STATUS_OK, 0);
  }
  last_ecn_sent_ = params.ecn_codepoint;
  return WriteResult(WRITE_STATUS_OK, last_packet_size_);
}

QuicPacketBuffer TestPacketWriter::GetNextWriteLocation(
    const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/) {
  return {AllocPacketBuffer(), [this](const char* p) { FreePacketBuffer(p); }};
}

WriteResult TestPacketWriter::Flush() {
  flush_attempts_++;
  if (block_on_next_flush_) {
    block_on_next_flush_ = false;
    SetWriteBlocked();
    return WriteResult(WRITE_STATUS_BLOCKED, /*errno*/ -1);
  }
  if (write_should_fail_) {
    return WriteResult(WRITE_STATUS_ERROR, /*errno*/ -1);
  }
  int bytes_flushed = bytes_buffered_;
  bytes_buffered_ = 0;
  return WriteResult(WRITE_STATUS_OK, bytes_flushed);
}

char* TestPacketWriter::AllocPacketBuffer() {
  PacketBuffer* p = packet_buffer_free_list_.front();
  EXPECT_FALSE(p->in_use);
  p->in_use = true;
  packet_buffer_free_list_.pop_front();
  return p->buffer;
}

void TestPacketWriter::FreePacketBuffer(const char* buffer) {
  auto iter = packet_buffer_pool_index_.find(const_cast<char*>(buffer));
  ASSERT_TRUE(iter != packet_buffer_pool_index_.end());
  PacketBuffer* p = iter->second;
  ASSERT_TRUE(p->in_use);
  p->in_use = false;
  packet_buffer_free_list_.push_back(p);
}

bool WriteServerVersionNegotiationProbeResponse(
    char* packet_bytes, size_t* packet_length_out,
    const char* source_connection_id_bytes,
    uint8_t source_connection_id_length) {
  if (packet_bytes == nullptr) {
    QUIC_BUG(quic_bug_10256_1) << "Invalid packet_bytes";
    return false;
  }
  if (packet_length_out == nullptr) {
    QUIC_BUG(quic_bug_10256_2) << "Invalid packet_length_out";
    return false;
  }
  QuicConnectionId source_connection_id(source_connection_id_bytes,
                                        source_connection_id_length);
  std::unique_ptr<QuicEncryptedPacket> encrypted_packet =
      QuicFramer::BuildVersionNegotiationPacket(
          source_connection_id, EmptyQuicConnectionId(),
          /*ietf_quic=*/true, /*use_length_prefix=*/true,
          ParsedQuicVersionVector{});
  if (!encrypted_packet) {
    QUIC_BUG(quic_bug_10256_3) << "Failed to create version negotiation packet";
    return false;
  }
  if (*packet_length_out < encrypted_packet->length()) {
    QUIC_BUG(quic_bug_10256_4)
        << "Invalid *packet_length_out " << *packet_length_out << " < "
        << encrypted_packet->length();
    return false;
  }
  *packet_length_out = encrypted_packet->length();
  memcpy(packet_bytes, encrypted_packet->data(), *packet_length_out);
  return true;
}

bool ParseClientVersionNegotiationProbePacket(
    const char* packet_bytes, size_t packet_length,
    char* destination_connection_id_bytes,
    uint8_t* destination_connection_id_length_out) {
  if (packet_bytes == nullptr) {
    QUIC_BUG(quic_bug_10256_5) << "Invalid packet_bytes";
    return false;
  }
  if (packet_length < kMinPacketSizeForVersionNegotiation ||
      packet_length > 65535) {
    QUIC_BUG(quic_bug_10256_6) << "Invalid packet_length";
    return false;
  }
  if (destination_connection_id_bytes == nullptr) {
    QUIC_BUG(quic_bug_10256_7) << "Invalid destination_connection_id_bytes";
    return false;
  }
  if (destination_connection_id_length_out == nullptr) {
    QUIC_BUG(quic_bug_10256_8)
        << "Invalid destination_connection_id_length_out";
    return false;
  }

  QuicEncryptedPacket encrypted_packet(packet_bytes, packet_length);
  PacketHeaderFormat format;
  QuicLongHeaderType long_packet_type;
  bool version_present, has_length_prefix;
  QuicVersionLabel version_label;
  ParsedQuicVersion parsed_version = ParsedQuicVersion::Unsupported();
  QuicConnectionId destination_connection_id, source_connection_id;
  absl::optional<absl::string_view> retry_token;
  std::string detailed_error;
  QuicErrorCode error = QuicFramer::ParsePublicHeaderDispatcher(
      encrypted_packet,
      /*expected_destination_connection_id_length=*/0, &format,
      &long_packet_type, &version_present, &has_length_prefix, &version_label,
      &parsed_version, &destination_connection_id, &source_connection_id,
      &retry_token, &detailed_error);
  if (error != QUIC_NO_ERROR) {
    QUIC_BUG(quic_bug_10256_9) << "Failed to parse packet: " << detailed_error;
    return false;
  }
  if (!version_present) {
    QUIC_BUG(quic_bug_10256_10) << "Packet is not a long header";
    return false;
  }
  if (*destination_connection_id_length_out <
      destination_connection_id.length()) {
    QUIC_BUG(quic_bug_10256_11)
        << "destination_connection_id_length_out too small";
    return false;
  }
  *destination_connection_id_length_out = destination_connection_id.length();
  memcpy(destination_connection_id_bytes, destination_connection_id.data(),
         *destination_connection_id_length_out);
  return true;
}

}  // namespace test
}  // namespace quic
