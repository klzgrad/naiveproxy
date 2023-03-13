// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Common utilities for Quic tests

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_UTILS_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_UTILS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/congestion_control/loss_detection_interface.h"
#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/crypto/transport_parameters.h"
#include "quiche/quic/core/http/http_decoder.h"
#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_sent_packet_manager.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/test_tools/mock_clock.h"
#include "quiche/quic/test_tools/mock_connection_id_generator.h"
#include "quiche/quic/test_tools/mock_quic_session_visitor.h"
#include "quiche/quic/test_tools/mock_random.h"
#include "quiche/quic/test_tools/quic_framer_peer.h"
#include "quiche/quic/test_tools/simple_quic_framer.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

namespace test {

// A generic predictable connection ID suited for testing.
QuicConnectionId TestConnectionId();

// A generic predictable connection ID suited for testing, generated from a
// given number, such as an index.
QuicConnectionId TestConnectionId(uint64_t connection_number);

// A generic predictable connection ID suited for testing, generated from a
// given number, such as an index. Guaranteed to be 9 bytes long.
QuicConnectionId TestConnectionIdNineBytesLong(uint64_t connection_number);

// Extracts the connection number passed to TestConnectionId().
uint64_t TestConnectionIdToUInt64(QuicConnectionId connection_id);

enum : uint16_t { kTestPort = 12345 };
enum : uint32_t {
  kMaxDatagramFrameSizeForTest = 1333,
  kMaxPacketSizeForTest = 9001,
  kInitialStreamFlowControlWindowForTest = 1024 * 1024,   // 1 MB
  kInitialSessionFlowControlWindowForTest = 1536 * 1024,  // 1.5 MB
};

enum : uint64_t {
  kAckDelayExponentForTest = 10,
  kMaxAckDelayForTest = 51,  // ms
  kActiveConnectionIdLimitForTest = 52,
  kMinAckDelayUsForTest = 1000
};

// Create an arbitrary stateless reset token, same across multiple calls.
std::vector<uint8_t> CreateStatelessResetTokenForTest();

// A hostname useful for testing, returns "test.example.org".
std::string TestHostname();

// A server ID useful for testing, returns test.example.org:12345.
QuicServerId TestServerId();

// Returns the test peer IP address.
QuicIpAddress TestPeerIPAddress();

// Upper limit on versions we support.
ParsedQuicVersion QuicVersionMax();

// Lower limit on versions we support.
ParsedQuicVersion QuicVersionMin();

// Disables all flags that enable QUIC versions that use TLS.
// This is only meant as a temporary measure to prevent some broken tests
// from running with TLS.
void DisableQuicVersionsWithTls();

// Create an encrypted packet for testing.
// If versions == nullptr, uses &AllSupportedVersions().
// Note that the packet is encrypted with NullEncrypter, so to decrypt the
// constructed packet, the framer must be set to use NullDecrypter.
QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data, bool full_padding,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions, Perspective perspective);

QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data, bool full_padding,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions);

// Create an encrypted packet for testing.
// If versions == nullptr, uses &AllSupportedVersions().
// Note that the packet is encrypted with NullEncrypter, so to decrypt the
// constructed packet, the framer must be set to use NullDecrypter.
QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length,
    ParsedQuicVersionVector* versions);

// This form assumes |versions| == nullptr.
QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length);

// This form assumes |connection_id_length| == PACKET_8BYTE_CONNECTION_ID,
// |packet_number_length| == PACKET_4BYTE_PACKET_NUMBER and
// |versions| == nullptr.
QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data);

// Creates a client-to-server ZERO-RTT packet that will fail to decrypt.
std::unique_ptr<QuicEncryptedPacket> GetUndecryptableEarlyPacket(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id);

// Constructs a received packet for testing. The caller must take ownership
// of the returned pointer.
QuicReceivedPacket* ConstructReceivedPacket(
    const QuicEncryptedPacket& encrypted_packet, QuicTime receipt_time);

// Create an encrypted packet for testing whose data portion erroneous.
// The specific way the data portion is erroneous is not specified, but
// it is an error that QuicFramer detects.
// Note that the packet is encrypted with NullEncrypter, so to decrypt the
// constructed packet, the framer must be set to use NullDecrypter.
QuicEncryptedPacket* ConstructMisFramedEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id, bool version_flag, bool reset_flag,
    uint64_t packet_number, const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length, ParsedQuicVersion version,
    Perspective perspective);

// Returns QuicConfig set to default values.
QuicConfig DefaultQuicConfig();

ParsedQuicVersionVector SupportedVersions(ParsedQuicVersion version);

struct QuicAckBlock {
  QuicPacketNumber start;  // Included
  QuicPacketNumber limit;  // Excluded
};

// Testing convenience method to construct a QuicAckFrame with arbitrary ack
// blocks. Each block is given by a (closed-open) range of packet numbers. e.g.:
// InitAckFrame({{1, 10}})
//   => 1 ack block acking packet numbers 1 to 9.
//
// InitAckFrame({{1, 2}, {3, 4}})
//   => 2 ack blocks acking packet 1 and 3. Packet 2 is missing.
QuicAckFrame InitAckFrame(const std::vector<QuicAckBlock>& ack_blocks);

// Testing convenience method to construct a QuicAckFrame with 1 ack block which
// covers packet number range [1, |largest_acked| + 1).
// Equivalent to InitAckFrame({{1, largest_acked + 1}})
QuicAckFrame InitAckFrame(uint64_t largest_acked);
QuicAckFrame InitAckFrame(QuicPacketNumber largest_acked);

// Testing convenience method to construct a QuicAckFrame with |num_ack_blocks|
// ack blocks of width 1 packet, starting from |least_unacked| + 2.
QuicAckFrame MakeAckFrameWithAckBlocks(size_t num_ack_blocks,
                                       uint64_t least_unacked);

// Testing convenice method to construct a QuicAckFrame with |largest_acked|,
// ack blocks of width 1 packet and |gap_size|.
QuicAckFrame MakeAckFrameWithGaps(uint64_t gap_size, size_t max_num_gaps,
                                  uint64_t largest_acked);

// Returns the encryption level that corresponds to the header type in
// |header|. If the header is for GOOGLE_QUIC_PACKET instead of an
// IETF-invariants packet, this function returns ENCRYPTION_INITIAL.
EncryptionLevel HeaderToEncryptionLevel(const QuicPacketHeader& header);

// Returns a QuicPacket that is owned by the caller, and
// is populated with the fields in |header| and |frames|, or is nullptr if the
// packet could not be created.
std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer, const QuicPacketHeader& header,
    const QuicFrames& frames);
// Returns a QuicPacket that is owned by the caller, and of size |packet_size|.
std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer, const QuicPacketHeader& header,
    const QuicFrames& frames, size_t packet_size);

// Compute SHA-1 hash of the supplied std::string.
std::string Sha1Hash(absl::string_view data);

// Delete |frame| and return true.
bool ClearControlFrame(const QuicFrame& frame);
bool ClearControlFrameWithTransmissionType(const QuicFrame& frame,
                                           TransmissionType type);

// Simple random number generator used to compute random numbers suitable
// for pseudo-randomly dropping packets in tests.
class SimpleRandom : public QuicRandom {
 public:
  SimpleRandom() { set_seed(0); }
  SimpleRandom(const SimpleRandom&) = delete;
  SimpleRandom& operator=(const SimpleRandom&) = delete;
  ~SimpleRandom() override {}

  // Generates |len| random bytes in the |data| buffer.
  void RandBytes(void* data, size_t len) override;
  // Returns a random number in the range [0, kuint64max].
  uint64_t RandUint64() override;

  // InsecureRandBytes behaves equivalently to RandBytes.
  void InsecureRandBytes(void* data, size_t len) override;
  // InsecureRandUint64 behaves equivalently to RandUint64.
  uint64_t InsecureRandUint64() override;

  void set_seed(uint64_t seed);

 private:
  uint8_t buffer_[4096];
  size_t buffer_offset_ = 0;
  uint8_t key_[32];

  void FillBuffer();
};

class MockFramerVisitor : public QuicFramerVisitorInterface {
 public:
  MockFramerVisitor();
  MockFramerVisitor(const MockFramerVisitor&) = delete;
  MockFramerVisitor& operator=(const MockFramerVisitor&) = delete;
  ~MockFramerVisitor() override;

  MOCK_METHOD(void, OnError, (QuicFramer*), (override));
  // The constructor sets this up to return false by default.
  MOCK_METHOD(bool, OnProtocolVersionMismatch, (ParsedQuicVersion version),
              (override));
  MOCK_METHOD(void, OnPacket, (), (override));
  MOCK_METHOD(void, OnPublicResetPacket, (const QuicPublicResetPacket& header),
              (override));
  MOCK_METHOD(void, OnVersionNegotiationPacket,
              (const QuicVersionNegotiationPacket& packet), (override));
  MOCK_METHOD(void, OnRetryPacket,
              (QuicConnectionId original_connection_id,
               QuicConnectionId new_connection_id,
               absl::string_view retry_token,
               absl::string_view retry_integrity_tag,
               absl::string_view retry_without_tag),
              (override));
  // The constructor sets this up to return true by default.
  MOCK_METHOD(bool, OnUnauthenticatedHeader, (const QuicPacketHeader& header),
              (override));
  // The constructor sets this up to return true by default.
  MOCK_METHOD(bool, OnUnauthenticatedPublicHeader,
              (const QuicPacketHeader& header), (override));
  MOCK_METHOD(void, OnDecryptedPacket, (size_t length, EncryptionLevel level),
              (override));
  MOCK_METHOD(bool, OnPacketHeader, (const QuicPacketHeader& header),
              (override));
  MOCK_METHOD(void, OnCoalescedPacket, (const QuicEncryptedPacket& packet),
              (override));
  MOCK_METHOD(void, OnUndecryptablePacket,
              (const QuicEncryptedPacket& packet,
               EncryptionLevel decryption_level, bool has_decryption_key),
              (override));
  MOCK_METHOD(bool, OnStreamFrame, (const QuicStreamFrame& frame), (override));
  MOCK_METHOD(bool, OnCryptoFrame, (const QuicCryptoFrame& frame), (override));
  MOCK_METHOD(bool, OnAckFrameStart, (QuicPacketNumber, QuicTime::Delta),
              (override));
  MOCK_METHOD(bool, OnAckRange, (QuicPacketNumber, QuicPacketNumber),
              (override));
  MOCK_METHOD(bool, OnAckTimestamp, (QuicPacketNumber, QuicTime), (override));
  MOCK_METHOD(void, OnAckEcnCounts, (const QuicEcnCounts&), (override));
  MOCK_METHOD(bool, OnAckFrameEnd, (QuicPacketNumber), (override));
  MOCK_METHOD(bool, OnStopWaitingFrame, (const QuicStopWaitingFrame& frame),
              (override));
  MOCK_METHOD(bool, OnPaddingFrame, (const QuicPaddingFrame& frame),
              (override));
  MOCK_METHOD(bool, OnPingFrame, (const QuicPingFrame& frame), (override));
  MOCK_METHOD(bool, OnRstStreamFrame, (const QuicRstStreamFrame& frame),
              (override));
  MOCK_METHOD(bool, OnConnectionCloseFrame,
              (const QuicConnectionCloseFrame& frame), (override));
  MOCK_METHOD(bool, OnNewConnectionIdFrame,
              (const QuicNewConnectionIdFrame& frame), (override));
  MOCK_METHOD(bool, OnRetireConnectionIdFrame,
              (const QuicRetireConnectionIdFrame& frame), (override));
  MOCK_METHOD(bool, OnNewTokenFrame, (const QuicNewTokenFrame& frame),
              (override));
  MOCK_METHOD(bool, OnStopSendingFrame, (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(bool, OnPathChallengeFrame, (const QuicPathChallengeFrame& frame),
              (override));
  MOCK_METHOD(bool, OnPathResponseFrame, (const QuicPathResponseFrame& frame),
              (override));
  MOCK_METHOD(bool, OnGoAwayFrame, (const QuicGoAwayFrame& frame), (override));
  MOCK_METHOD(bool, OnMaxStreamsFrame, (const QuicMaxStreamsFrame& frame),
              (override));
  MOCK_METHOD(bool, OnStreamsBlockedFrame,
              (const QuicStreamsBlockedFrame& frame), (override));
  MOCK_METHOD(bool, OnWindowUpdateFrame, (const QuicWindowUpdateFrame& frame),
              (override));
  MOCK_METHOD(bool, OnBlockedFrame, (const QuicBlockedFrame& frame),
              (override));
  MOCK_METHOD(bool, OnMessageFrame, (const QuicMessageFrame& frame),
              (override));
  MOCK_METHOD(bool, OnHandshakeDoneFrame, (const QuicHandshakeDoneFrame& frame),
              (override));
  MOCK_METHOD(bool, OnAckFrequencyFrame, (const QuicAckFrequencyFrame& frame),
              (override));
  MOCK_METHOD(void, OnPacketComplete, (), (override));
  MOCK_METHOD(bool, IsValidStatelessResetToken, (const StatelessResetToken&),
              (const, override));
  MOCK_METHOD(void, OnAuthenticatedIetfStatelessResetPacket,
              (const QuicIetfStatelessResetPacket&), (override));
  MOCK_METHOD(void, OnKeyUpdate, (KeyUpdateReason), (override));
  MOCK_METHOD(void, OnDecryptedFirstPacketInKeyPhase, (), (override));
  MOCK_METHOD(std::unique_ptr<QuicDecrypter>,
              AdvanceKeysAndCreateCurrentOneRttDecrypter, (), (override));
  MOCK_METHOD(std::unique_ptr<QuicEncrypter>, CreateCurrentOneRttEncrypter, (),
              (override));
};

class NoOpFramerVisitor : public QuicFramerVisitorInterface {
 public:
  NoOpFramerVisitor() {}
  NoOpFramerVisitor(const NoOpFramerVisitor&) = delete;
  NoOpFramerVisitor& operator=(const NoOpFramerVisitor&) = delete;

  void OnError(QuicFramer* /*framer*/) override {}
  void OnPacket() override {}
  void OnPublicResetPacket(const QuicPublicResetPacket& /*packet*/) override {}
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& /*packet*/) override {}
  void OnRetryPacket(QuicConnectionId /*original_connection_id*/,
                     QuicConnectionId /*new_connection_id*/,
                     absl::string_view /*retry_token*/,
                     absl::string_view /*retry_integrity_tag*/,
                     absl::string_view /*retry_without_tag*/) override {}
  bool OnProtocolVersionMismatch(ParsedQuicVersion version) override;
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override;
  bool OnUnauthenticatedPublicHeader(const QuicPacketHeader& header) override;
  void OnDecryptedPacket(size_t /*length*/,
                         EncryptionLevel /*level*/) override {}
  bool OnPacketHeader(const QuicPacketHeader& header) override;
  void OnCoalescedPacket(const QuicEncryptedPacket& packet) override;
  void OnUndecryptablePacket(const QuicEncryptedPacket& packet,
                             EncryptionLevel decryption_level,
                             bool has_decryption_key) override;
  bool OnStreamFrame(const QuicStreamFrame& frame) override;
  bool OnCryptoFrame(const QuicCryptoFrame& frame) override;
  bool OnAckFrameStart(QuicPacketNumber largest_acked,
                       QuicTime::Delta ack_delay_time) override;
  bool OnAckRange(QuicPacketNumber start, QuicPacketNumber end) override;
  bool OnAckTimestamp(QuicPacketNumber packet_number,
                      QuicTime timestamp) override;
  void OnAckEcnCounts(const QuicEcnCounts& ecn_counts) override;
  bool OnAckFrameEnd(QuicPacketNumber start) override;
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override;
  bool OnPaddingFrame(const QuicPaddingFrame& frame) override;
  bool OnPingFrame(const QuicPingFrame& frame) override;
  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override;
  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override;
  bool OnNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame) override;
  bool OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame) override;
  bool OnNewTokenFrame(const QuicNewTokenFrame& frame) override;
  bool OnStopSendingFrame(const QuicStopSendingFrame& frame) override;
  bool OnPathChallengeFrame(const QuicPathChallengeFrame& frame) override;
  bool OnPathResponseFrame(const QuicPathResponseFrame& frame) override;
  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override;
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) override;
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override;
  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override;
  bool OnBlockedFrame(const QuicBlockedFrame& frame) override;
  bool OnMessageFrame(const QuicMessageFrame& frame) override;
  bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) override;
  bool OnAckFrequencyFrame(const QuicAckFrequencyFrame& frame) override;
  void OnPacketComplete() override {}
  bool IsValidStatelessResetToken(
      const StatelessResetToken& token) const override;
  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& /*packet*/) override {}
  void OnKeyUpdate(KeyUpdateReason /*reason*/) override {}
  void OnDecryptedFirstPacketInKeyPhase() override {}
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override {
    return nullptr;
  }
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override {
    return nullptr;
  }
};

class MockQuicConnectionVisitor : public QuicConnectionVisitorInterface {
 public:
  MockQuicConnectionVisitor();
  MockQuicConnectionVisitor(const MockQuicConnectionVisitor&) = delete;
  MockQuicConnectionVisitor& operator=(const MockQuicConnectionVisitor&) =
      delete;
  ~MockQuicConnectionVisitor() override;

  MOCK_METHOD(void, OnStreamFrame, (const QuicStreamFrame& frame), (override));
  MOCK_METHOD(void, OnCryptoFrame, (const QuicCryptoFrame& frame), (override));
  MOCK_METHOD(void, OnWindowUpdateFrame, (const QuicWindowUpdateFrame& frame),
              (override));
  MOCK_METHOD(void, OnBlockedFrame, (const QuicBlockedFrame& frame),
              (override));
  MOCK_METHOD(void, OnRstStream, (const QuicRstStreamFrame& frame), (override));
  MOCK_METHOD(void, OnGoAway, (const QuicGoAwayFrame& frame), (override));
  MOCK_METHOD(void, OnMessageReceived, (absl::string_view message), (override));
  MOCK_METHOD(void, OnHandshakeDoneReceived, (), (override));
  MOCK_METHOD(void, OnNewTokenReceived, (absl::string_view token), (override));
  MOCK_METHOD(void, OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(void, OnWriteBlocked, (), (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
  MOCK_METHOD(void, OnCongestionWindowChange, (QuicTime now), (override));
  MOCK_METHOD(void, OnConnectionMigration, (AddressChangeType type),
              (override));
  MOCK_METHOD(void, OnPathDegrading, (), (override));
  MOCK_METHOD(void, OnForwardProgressMadeAfterPathDegrading, (), (override));
  MOCK_METHOD(bool, WillingAndAbleToWrite, (), (const, override));
  MOCK_METHOD(bool, ShouldKeepConnectionAlive, (), (const, override));
  MOCK_METHOD(std::string, GetStreamsInfoForLogging, (), (const, override));
  MOCK_METHOD(void, OnSuccessfulVersionNegotiation,
              (const ParsedQuicVersion& version), (override));
  MOCK_METHOD(void, OnPacketReceived,
              (const QuicSocketAddress& self_address,
               const QuicSocketAddress& peer_address,
               bool is_connectivity_probe),
              (override));
  MOCK_METHOD(void, OnAckNeedsRetransmittableFrame, (), (override));
  MOCK_METHOD(void, SendAckFrequency, (const QuicAckFrequencyFrame& frame),
              (override));
  MOCK_METHOD(void, SendNewConnectionId,
              (const QuicNewConnectionIdFrame& frame), (override));
  MOCK_METHOD(void, SendRetireConnectionId, (uint64_t sequence_number),
              (override));
  MOCK_METHOD(bool, MaybeReserveConnectionId,
              (const QuicConnectionId& server_connection_id), (override));
  MOCK_METHOD(void, OnServerConnectionIdRetired,
              (const QuicConnectionId& server_connection_id), (override));
  MOCK_METHOD(bool, AllowSelfAddressChange, (), (const, override));
  MOCK_METHOD(HandshakeState, GetHandshakeState, (), (const, override));
  MOCK_METHOD(bool, OnMaxStreamsFrame, (const QuicMaxStreamsFrame& frame),
              (override));
  MOCK_METHOD(bool, OnStreamsBlockedFrame,
              (const QuicStreamsBlockedFrame& frame), (override));
  MOCK_METHOD(void, OnStopSendingFrame, (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(void, OnPacketDecrypted, (EncryptionLevel), (override));
  MOCK_METHOD(void, OnOneRttPacketAcknowledged, (), (override));
  MOCK_METHOD(void, OnHandshakePacketSent, (), (override));
  MOCK_METHOD(void, OnKeyUpdate, (KeyUpdateReason), (override));
  MOCK_METHOD(std::unique_ptr<QuicDecrypter>,
              AdvanceKeysAndCreateCurrentOneRttDecrypter, (), (override));
  MOCK_METHOD(std::unique_ptr<QuicEncrypter>, CreateCurrentOneRttEncrypter, (),
              (override));
  MOCK_METHOD(void, BeforeConnectionCloseSent, (), (override));
  MOCK_METHOD(bool, ValidateToken, (absl::string_view), (override));
  MOCK_METHOD(bool, MaybeSendAddressToken, (), (override));
  MOCK_METHOD(std::unique_ptr<QuicPathValidationContext>,
              CreateContextForMultiPortPath, (), (override));
  MOCK_METHOD(void, OnServerPreferredAddressAvailable,
              (const QuicSocketAddress&), (override));
  void OnBandwidthUpdateTimeout() override {}
};

class MockQuicConnectionHelper : public QuicConnectionHelperInterface {
 public:
  MockQuicConnectionHelper();
  MockQuicConnectionHelper(const MockQuicConnectionHelper&) = delete;
  MockQuicConnectionHelper& operator=(const MockQuicConnectionHelper&) = delete;
  ~MockQuicConnectionHelper() override;
  const QuicClock* GetClock() const override;
  QuicClock* GetClock();
  QuicRandom* GetRandomGenerator() override;
  quiche::QuicheBufferAllocator* GetStreamSendBufferAllocator() override;
  void AdvanceTime(QuicTime::Delta delta);

 private:
  MockClock clock_;
  testing::NiceMock<MockRandom> random_generator_;
  quiche::SimpleBufferAllocator buffer_allocator_;
};

class MockAlarmFactory : public QuicAlarmFactory {
 public:
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

  // No-op alarm implementation
  class TestAlarm : public QuicAlarm {
   public:
    explicit TestAlarm(QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
        : QuicAlarm(std::move(delegate)) {}

    void SetImpl() override {}
    void CancelImpl() override {}

    using QuicAlarm::Fire;
  };

  void FireAlarm(QuicAlarm* alarm) {
    reinterpret_cast<TestAlarm*>(alarm)->Fire();
  }
};

class TestAlarmFactory : public QuicAlarmFactory {
 public:
  class TestAlarm : public QuicAlarm {
   public:
    explicit TestAlarm(QuicArenaScopedPtr<QuicAlarm::Delegate> delegate)
        : QuicAlarm(std::move(delegate)) {}

    void SetImpl() override {}
    void CancelImpl() override {}
    using QuicAlarm::Fire;
  };

  TestAlarmFactory() {}
  TestAlarmFactory(const TestAlarmFactory&) = delete;
  TestAlarmFactory& operator=(const TestAlarmFactory&) = delete;

  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override {
    return new TestAlarm(QuicArenaScopedPtr<QuicAlarm::Delegate>(delegate));
  }

  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override {
    return arena->New<TestAlarm>(std::move(delegate));
  }
};

class MockQuicConnection : public QuicConnection {
 public:
  // Uses a ConnectionId of 42 and 127.0.0.1:123.
  MockQuicConnection(QuicConnectionHelperInterface* helper,
                     QuicAlarmFactory* alarm_factory, Perspective perspective);

  // Uses a ConnectionId of 42.
  MockQuicConnection(QuicSocketAddress address,
                     QuicConnectionHelperInterface* helper,
                     QuicAlarmFactory* alarm_factory, Perspective perspective);

  // Uses 127.0.0.1:123.
  MockQuicConnection(QuicConnectionId connection_id,
                     QuicConnectionHelperInterface* helper,
                     QuicAlarmFactory* alarm_factory, Perspective perspective);

  // Uses a ConnectionId of 42, and 127.0.0.1:123.
  MockQuicConnection(QuicConnectionHelperInterface* helper,
                     QuicAlarmFactory* alarm_factory, Perspective perspective,
                     const ParsedQuicVersionVector& supported_versions);

  MockQuicConnection(QuicConnectionId connection_id, QuicSocketAddress address,
                     QuicConnectionHelperInterface* helper,
                     QuicAlarmFactory* alarm_factory, Perspective perspective,
                     const ParsedQuicVersionVector& supported_versions);
  MockQuicConnection(const MockQuicConnection&) = delete;
  MockQuicConnection& operator=(const MockQuicConnection&) = delete;

  ~MockQuicConnection() override;

  // If the constructor that uses a QuicConnectionHelperInterface has been used
  // then this method will advance the time of the MockClock.
  void AdvanceTime(QuicTime::Delta delta);

  MOCK_METHOD(void, ProcessUdpPacket,
              (const QuicSocketAddress& self_address,
               const QuicSocketAddress& peer_address,
               const QuicReceivedPacket& packet),
              (override));
  MOCK_METHOD(void, CloseConnection,
              (QuicErrorCode error, const std::string& details,
               ConnectionCloseBehavior connection_close_behavior),
              (override));
  MOCK_METHOD(void, CloseConnection,
              (QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
               const std::string& details,
               ConnectionCloseBehavior connection_close_behavior),
              (override));
  MOCK_METHOD(void, SendConnectionClosePacket,
              (QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
               const std::string& details),
              (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
  MOCK_METHOD(bool, SendConnectivityProbingPacket,
              (QuicPacketWriter*, const QuicSocketAddress& peer_address),
              (override));
  MOCK_METHOD(void, MaybeProbeMultiPortPath, (), (override));

  MOCK_METHOD(void, OnSendConnectionState, (const CachedNetworkParameters&),
              (override));
  MOCK_METHOD(void, ResumeConnectionState,
              (const CachedNetworkParameters&, bool), (override));
  MOCK_METHOD(void, SetMaxPacingRate, (QuicBandwidth), (override));

  MOCK_METHOD(void, OnStreamReset, (QuicStreamId, QuicRstStreamErrorCode),
              (override));
  MOCK_METHOD(bool, SendControlFrame, (const QuicFrame& frame), (override));
  MOCK_METHOD(MessageStatus, SendMessage,
              (QuicMessageId, absl::Span<quiche::QuicheMemSlice>, bool),
              (override));
  MOCK_METHOD(bool, SendPathChallenge,
              (const QuicPathFrameBuffer&, const QuicSocketAddress&,
               const QuicSocketAddress&, const QuicSocketAddress&,
               QuicPacketWriter*),
              (override));

  MOCK_METHOD(void, OnError, (QuicFramer*), (override));
  void QuicConnection_OnError(QuicFramer* framer) {
    QuicConnection::OnError(framer);
  }

  void ReallyOnCanWrite() { QuicConnection::OnCanWrite(); }

  void ReallyCloseConnection(
      QuicErrorCode error, const std::string& details,
      ConnectionCloseBehavior connection_close_behavior) {
    // Call the 4-param method directly instead of the 3-param method, so that
    // it doesn't invoke the virtual 4-param method causing the mock 4-param
    // method to trigger.
    QuicConnection::CloseConnection(error, NO_IETF_QUIC_ERROR, details,
                                    connection_close_behavior);
  }

  void ReallyCloseConnection4(
      QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
      const std::string& details,
      ConnectionCloseBehavior connection_close_behavior) {
    QuicConnection::CloseConnection(error, ietf_error, details,
                                    connection_close_behavior);
  }

  void ReallySendConnectionClosePacket(QuicErrorCode error,
                                       QuicIetfTransportErrorCodes ietf_error,
                                       const std::string& details) {
    QuicConnection::SendConnectionClosePacket(error, ietf_error, details);
  }

  void ReallyProcessUdpPacket(const QuicSocketAddress& self_address,
                              const QuicSocketAddress& peer_address,
                              const QuicReceivedPacket& packet) {
    QuicConnection::ProcessUdpPacket(self_address, peer_address, packet);
  }

  bool OnProtocolVersionMismatch(ParsedQuicVersion version) override;
  void OnIdleNetworkDetected() override {}

  bool ReallySendControlFrame(const QuicFrame& frame) {
    return QuicConnection::SendControlFrame(frame);
  }

  bool ReallySendConnectivityProbingPacket(
      QuicPacketWriter* probing_writer, const QuicSocketAddress& peer_address) {
    return QuicConnection::SendConnectivityProbingPacket(probing_writer,
                                                         peer_address);
  }

  bool ReallyOnPathResponseFrame(const QuicPathResponseFrame& frame) {
    return QuicConnection::OnPathResponseFrame(frame);
  }

  MOCK_METHOD(bool, OnPathResponseFrame, (const QuicPathResponseFrame&),
              (override));
  MOCK_METHOD(bool, OnStopSendingFrame, (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(size_t, SendCryptoData,
              (EncryptionLevel, size_t, QuicStreamOffset), (override));
  size_t QuicConnection_SendCryptoData(EncryptionLevel level,
                                       size_t write_length,
                                       QuicStreamOffset offset) {
    return QuicConnection::SendCryptoData(level, write_length, offset);
  }

  MockConnectionIdGenerator& connection_id_generator() {
    return connection_id_generator_;
  }

 private:
  // It would be more correct to pass the generator as an argument to the
  // constructor, particularly in dispatcher tests that keep their own
  // reference to a generator. But there are many, many instances of derived
  // test classes that would have to declare a generator. As this object is
  // public, it is straightforward for the caller to use it as an argument to
  // EXPECT_CALL.
  MockConnectionIdGenerator connection_id_generator_;
};

class PacketSavingConnection : public MockQuicConnection {
 public:
  PacketSavingConnection(QuicConnectionHelperInterface* helper,
                         QuicAlarmFactory* alarm_factory,
                         Perspective perspective);

  PacketSavingConnection(QuicConnectionHelperInterface* helper,
                         QuicAlarmFactory* alarm_factory,
                         Perspective perspective,
                         const ParsedQuicVersionVector& supported_versions);
  PacketSavingConnection(const PacketSavingConnection&) = delete;
  PacketSavingConnection& operator=(const PacketSavingConnection&) = delete;

  ~PacketSavingConnection() override;

  SerializedPacketFate GetSerializedPacketFate(
      bool is_mtu_discovery, EncryptionLevel encryption_level) override;

  void SendOrQueuePacket(SerializedPacket packet) override;

  MOCK_METHOD(void, OnPacketSent, (EncryptionLevel, TransmissionType));

  std::vector<std::unique_ptr<QuicEncryptedPacket>> encrypted_packets_;
  // Number of packets in encrypted_packets that has been delivered to the peer
  // connection.
  size_t number_of_packets_delivered_ = 0;
  MockClock clock_;
};

class MockQuicSession : public QuicSession {
 public:
  // Takes ownership of |connection|.
  MockQuicSession(QuicConnection* connection, bool create_mock_crypto_stream);

  // Takes ownership of |connection|.
  explicit MockQuicSession(QuicConnection* connection);
  MockQuicSession(const MockQuicSession&) = delete;
  MockQuicSession& operator=(const MockQuicSession&) = delete;
  ~MockQuicSession() override;

  QuicCryptoStream* GetMutableCryptoStream() override;
  const QuicCryptoStream* GetCryptoStream() const override;
  void SetCryptoStream(QuicCryptoStream* crypto_stream);

  MOCK_METHOD(void, OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(QuicStream*, CreateIncomingStream, (QuicStreamId id), (override));
  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (PendingStream*),
              (override));
  MOCK_METHOD(QuicConsumedData, WritevData,
              (QuicStreamId id, size_t write_length, QuicStreamOffset offset,
               StreamSendingState state, TransmissionType type,
               EncryptionLevel level),
              (override));
  MOCK_METHOD(bool, WriteControlFrame,
              (const QuicFrame& frame, TransmissionType type), (override));
  MOCK_METHOD(void, MaybeSendRstStreamFrame,
              (QuicStreamId stream_id, QuicResetStreamError error,
               QuicStreamOffset bytes_written),
              (override));
  MOCK_METHOD(void, MaybeSendStopSendingFrame,
              (QuicStreamId stream_id, QuicResetStreamError error), (override));
  MOCK_METHOD(void, SendBlocked,
              (QuicStreamId stream_id, QuicStreamOffset offset), (override));

  MOCK_METHOD(bool, ShouldKeepConnectionAlive, (), (const, override));
  MOCK_METHOD(std::vector<std::string>, GetAlpnsToOffer, (), (const, override));
  MOCK_METHOD(std::vector<absl::string_view>::const_iterator, SelectAlpn,
              (const std::vector<absl::string_view>&), (const, override));
  MOCK_METHOD(void, OnAlpnSelected, (absl::string_view), (override));

  using QuicSession::ActivateStream;

  // Returns a QuicConsumedData that indicates all of |write_length| (and |fin|
  // if set) has been consumed.
  QuicConsumedData ConsumeData(QuicStreamId id, size_t write_length,
                               QuicStreamOffset offset,
                               StreamSendingState state, TransmissionType type,
                               absl::optional<EncryptionLevel> level);

  void ReallyMaybeSendRstStreamFrame(QuicStreamId id,
                                     QuicRstStreamErrorCode error,
                                     QuicStreamOffset bytes_written) {
    QuicSession::MaybeSendRstStreamFrame(
        id, QuicResetStreamError::FromInternal(error), bytes_written);
  }

 private:
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
};

class MockQuicCryptoStream : public QuicCryptoStream {
 public:
  explicit MockQuicCryptoStream(QuicSession* session);

  ~MockQuicCryptoStream() override;

  ssl_early_data_reason_t EarlyDataReason() const override;
  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  void OnPacketDecrypted(EncryptionLevel /*level*/) override {}
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakePacketSent() override {}
  void OnHandshakeDoneReceived() override {}
  void OnNewTokenReceived(absl::string_view /*token*/) override {}
  std::string GetAddressToken(
      const CachedNetworkParameters* /*cached_network_parameters*/)
      const override {
    return "";
  }
  bool ValidateAddressToken(absl::string_view /*token*/) const override {
    return true;
  }
  const CachedNetworkParameters* PreviousCachedNetworkParams() const override {
    return nullptr;
  }
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters /*cached_network_params*/) override {}
  void OnConnectionClosed(QuicErrorCode /*error*/,
                          ConnectionCloseSource /*source*/) override {}
  HandshakeState GetHandshakeState() const override { return HANDSHAKE_START; }
  void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> /*application_state*/) override {}
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override {
    return nullptr;
  }
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override {
    return nullptr;
  }
  bool ExportKeyingMaterial(absl::string_view /*label*/,
                            absl::string_view /*context*/,
                            size_t /*result_len*/,
                            std::string* /*result*/) override {
    return false;
  }
  SSL* GetSsl() const override { return nullptr; }
  bool IsCryptoFrameExpectedForEncryptionLevel(
      quic::EncryptionLevel level) const override {
    return level != ENCRYPTION_ZERO_RTT;
  }
  EncryptionLevel GetEncryptionLevelToSendCryptoDataOfSpace(
      PacketNumberSpace space) const override {
    switch (space) {
      case INITIAL_DATA:
        return ENCRYPTION_INITIAL;
      case HANDSHAKE_DATA:
        return ENCRYPTION_HANDSHAKE;
      case APPLICATION_DATA:
        return ENCRYPTION_FORWARD_SECURE;
      default:
        QUICHE_DCHECK(false);
        return NUM_ENCRYPTION_LEVELS;
    }
  }

 private:
  quiche::QuicheReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
  CryptoFramer crypto_framer_;
};

class MockQuicSpdySession : public QuicSpdySession {
 public:
  // Takes ownership of |connection|.
  explicit MockQuicSpdySession(QuicConnection* connection);
  // Takes ownership of |connection|.
  MockQuicSpdySession(QuicConnection* connection,
                      bool create_mock_crypto_stream);
  MockQuicSpdySession(const MockQuicSpdySession&) = delete;
  MockQuicSpdySession& operator=(const MockQuicSpdySession&) = delete;
  ~MockQuicSpdySession() override;

  QuicCryptoStream* GetMutableCryptoStream() override;
  const QuicCryptoStream* GetCryptoStream() const override;
  void SetCryptoStream(QuicCryptoStream* crypto_stream);

  void ReallyOnConnectionClosed(const QuicConnectionCloseFrame& frame,
                                ConnectionCloseSource source) {
    QuicSession::OnConnectionClosed(frame, source);
  }

  // From QuicSession.
  MOCK_METHOD(void, OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (PendingStream*),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateOutgoingBidirectionalStream, (),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateOutgoingUnidirectionalStream, (),
              (override));
  MOCK_METHOD(bool, ShouldCreateIncomingStream, (QuicStreamId id), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(QuicConsumedData, WritevData,
              (QuicStreamId id, size_t write_length, QuicStreamOffset offset,
               StreamSendingState state, TransmissionType type,
               EncryptionLevel level),
              (override));
  MOCK_METHOD(void, MaybeSendRstStreamFrame,
              (QuicStreamId stream_id, QuicResetStreamError error,
               QuicStreamOffset bytes_written),
              (override));
  MOCK_METHOD(void, MaybeSendStopSendingFrame,
              (QuicStreamId stream_id, QuicResetStreamError error), (override));
  MOCK_METHOD(void, SendWindowUpdate,
              (QuicStreamId id, QuicStreamOffset byte_offset), (override));
  MOCK_METHOD(void, SendBlocked,
              (QuicStreamId id, QuicStreamOffset byte_offset), (override));
  MOCK_METHOD(void, OnStreamHeadersPriority,
              (QuicStreamId stream_id,
               const spdy::SpdyStreamPrecedence& precedence),
              (override));
  MOCK_METHOD(void, OnStreamHeaderList,
              (QuicStreamId stream_id, bool fin, size_t frame_len,
               const QuicHeaderList& header_list),
              (override));
  MOCK_METHOD(void, OnPromiseHeaderList,
              (QuicStreamId stream_id, QuicStreamId promised_stream_id,
               size_t frame_len, const QuicHeaderList& header_list),
              (override));
  MOCK_METHOD(void, OnPriorityFrame,
              (QuicStreamId id, const spdy::SpdyStreamPrecedence& precedence),
              (override));
  MOCK_METHOD(void, OnCongestionWindowChange, (QuicTime now), (override));

  // Returns a QuicConsumedData that indicates all of |write_length| (and |fin|
  // if set) has been consumed.
  QuicConsumedData ConsumeData(QuicStreamId id, size_t write_length,
                               QuicStreamOffset offset,
                               StreamSendingState state, TransmissionType type,
                               absl::optional<EncryptionLevel> level);

  using QuicSession::ActivateStream;

 private:
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
};

class MockHttp3DebugVisitor : public Http3DebugVisitor {
 public:
  MOCK_METHOD(void, OnControlStreamCreated, (QuicStreamId), (override));
  MOCK_METHOD(void, OnQpackEncoderStreamCreated, (QuicStreamId), (override));
  MOCK_METHOD(void, OnQpackDecoderStreamCreated, (QuicStreamId), (override));
  MOCK_METHOD(void, OnPeerControlStreamCreated, (QuicStreamId), (override));
  MOCK_METHOD(void, OnPeerQpackEncoderStreamCreated, (QuicStreamId),
              (override));
  MOCK_METHOD(void, OnPeerQpackDecoderStreamCreated, (QuicStreamId),
              (override));

  MOCK_METHOD(void, OnSettingsFrameReceivedViaAlps, (const SettingsFrame&),
              (override));

  MOCK_METHOD(void, OnAcceptChFrameReceivedViaAlps, (const AcceptChFrame&),
              (override));

  MOCK_METHOD(void, OnSettingsFrameReceived, (const SettingsFrame&),
              (override));
  MOCK_METHOD(void, OnGoAwayFrameReceived, (const GoAwayFrame&), (override));
  MOCK_METHOD(void, OnPriorityUpdateFrameReceived, (const PriorityUpdateFrame&),
              (override));
  MOCK_METHOD(void, OnAcceptChFrameReceived, (const AcceptChFrame&),
              (override));

  MOCK_METHOD(void, OnDataFrameReceived, (QuicStreamId, QuicByteCount),
              (override));
  MOCK_METHOD(void, OnHeadersFrameReceived, (QuicStreamId, QuicByteCount),
              (override));
  MOCK_METHOD(void, OnHeadersDecoded, (QuicStreamId, QuicHeaderList),
              (override));
  MOCK_METHOD(void, OnUnknownFrameReceived,
              (QuicStreamId, uint64_t, QuicByteCount), (override));

  MOCK_METHOD(void, OnSettingsFrameSent, (const SettingsFrame&), (override));
  MOCK_METHOD(void, OnGoAwayFrameSent, (QuicStreamId), (override));
  MOCK_METHOD(void, OnPriorityUpdateFrameSent, (const PriorityUpdateFrame&),
              (override));

  MOCK_METHOD(void, OnDataFrameSent, (QuicStreamId, QuicByteCount), (override));
  MOCK_METHOD(void, OnHeadersFrameSent,
              (QuicStreamId, const spdy::Http2HeaderBlock&), (override));
  MOCK_METHOD(void, OnSettingsFrameResumed, (const SettingsFrame&), (override));
};

class TestQuicSpdyServerSession : public QuicServerSessionBase {
 public:
  // Takes ownership of |connection|.
  TestQuicSpdyServerSession(QuicConnection* connection,
                            const QuicConfig& config,
                            const ParsedQuicVersionVector& supported_versions,
                            const QuicCryptoServerConfig* crypto_config,
                            QuicCompressedCertsCache* compressed_certs_cache);
  TestQuicSpdyServerSession(const TestQuicSpdyServerSession&) = delete;
  TestQuicSpdyServerSession& operator=(const TestQuicSpdyServerSession&) =
      delete;
  ~TestQuicSpdyServerSession() override;

  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (PendingStream*),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateOutgoingBidirectionalStream, (),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateOutgoingUnidirectionalStream, (),
              (override));
  MOCK_METHOD(std::vector<absl::string_view>::const_iterator, SelectAlpn,
              (const std::vector<absl::string_view>&), (const, override));
  MOCK_METHOD(void, OnAlpnSelected, (absl::string_view), (override));
  std::unique_ptr<QuicCryptoServerStreamBase> CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override;

  QuicCryptoServerStreamBase* GetMutableCryptoStream() override;

  const QuicCryptoServerStreamBase* GetCryptoStream() const override;

  MockQuicCryptoServerStreamHelper* helper() { return &helper_; }

  QuicSSLConfig GetSSLConfig() const override {
    QuicSSLConfig ssl_config = QuicServerSessionBase::GetSSLConfig();
    if (early_data_enabled_.has_value()) {
      ssl_config.early_data_enabled = *early_data_enabled_;
    }
    if (client_cert_mode_.has_value()) {
      ssl_config.client_cert_mode = *client_cert_mode_;
    }

    return ssl_config;
  }

  void set_early_data_enabled(bool enabled) { early_data_enabled_ = enabled; }

  void set_client_cert_mode(ClientCertMode mode) { client_cert_mode_ = mode; }

 private:
  MockQuicSessionVisitor visitor_;
  MockQuicCryptoServerStreamHelper helper_;
  // If not nullopt, override the early_data_enabled value from base class'
  // ssl_config.
  absl::optional<bool> early_data_enabled_;
  // If not nullopt, override the client_cert_mode value from base class'
  // ssl_config.
  absl::optional<ClientCertMode> client_cert_mode_;
};

// A test implementation of QuicClientPushPromiseIndex::Delegate.
class TestPushPromiseDelegate : public QuicClientPushPromiseIndex::Delegate {
 public:
  // |match| sets the validation result for checking whether designated header
  // fields match for promise request and client request.
  explicit TestPushPromiseDelegate(bool match);

  bool CheckVary(const spdy::Http2HeaderBlock& client_request,
                 const spdy::Http2HeaderBlock& promise_request,
                 const spdy::Http2HeaderBlock& promise_response) override;

  void OnRendezvousResult(QuicSpdyStream* stream) override;

  QuicSpdyStream* rendezvous_stream() { return rendezvous_stream_; }
  bool rendezvous_fired() { return rendezvous_fired_; }

 private:
  bool match_;
  bool rendezvous_fired_;
  QuicSpdyStream* rendezvous_stream_;
};

class TestQuicSpdyClientSession : public QuicSpdyClientSessionBase {
 public:
  TestQuicSpdyClientSession(QuicConnection* connection,
                            const QuicConfig& config,
                            const ParsedQuicVersionVector& supported_versions,
                            const QuicServerId& server_id,
                            QuicCryptoClientConfig* crypto_config);
  TestQuicSpdyClientSession(const TestQuicSpdyClientSession&) = delete;
  TestQuicSpdyClientSession& operator=(const TestQuicSpdyClientSession&) =
      delete;
  ~TestQuicSpdyClientSession() override;

  bool IsAuthorized(const std::string& authority) override;

  // QuicSpdyClientSessionBase
  MOCK_METHOD(void, OnProofValid,
              (const QuicCryptoClientConfig::CachedState& cached), (override));
  MOCK_METHOD(void, OnProofVerifyDetailsAvailable,
              (const ProofVerifyDetails& verify_details), (override));

  // TestQuicSpdyClientSession
  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateIncomingStream, (PendingStream*),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateOutgoingBidirectionalStream, (),
              (override));
  MOCK_METHOD(QuicSpdyStream*, CreateOutgoingUnidirectionalStream, (),
              (override));
  MOCK_METHOD(bool, ShouldCreateIncomingStream, (QuicStreamId id), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(std::vector<std::string>, GetAlpnsToOffer, (), (const, override));
  MOCK_METHOD(void, OnAlpnSelected, (absl::string_view), (override));
  MOCK_METHOD(void, OnConfigNegotiated, (), (override));

  QuicCryptoClientStream* GetMutableCryptoStream() override;
  const QuicCryptoClientStream* GetCryptoStream() const override;

  // Override to save sent crypto handshake messages.
  void OnCryptoHandshakeMessageSent(
      const CryptoHandshakeMessage& message) override {
    sent_crypto_handshake_messages_.push_back(message);
  }

  const std::vector<CryptoHandshakeMessage>& sent_crypto_handshake_messages()
      const {
    return sent_crypto_handshake_messages_;
  }

 private:
  // Calls the parent class's OnConfigNegotiated method. Used to set the default
  // mock behavior for OnConfigNegotiated.
  void RealOnConfigNegotiated();

  std::unique_ptr<QuicCryptoClientStream> crypto_stream_;
  QuicClientPushPromiseIndex push_promise_index_;
  std::vector<CryptoHandshakeMessage> sent_crypto_handshake_messages_;
};

class MockPacketWriter : public QuicPacketWriter {
 public:
  MockPacketWriter();
  MockPacketWriter(const MockPacketWriter&) = delete;
  MockPacketWriter& operator=(const MockPacketWriter&) = delete;
  ~MockPacketWriter() override;

  MOCK_METHOD(WriteResult, WritePacket,
              (const char*, size_t buf_len, const QuicIpAddress& self_address,
               const QuicSocketAddress& peer_address, PerPacketOptions*),
              (override));
  MOCK_METHOD(bool, IsWriteBlocked, (), (const, override));
  MOCK_METHOD(void, SetWritable, (), (override));
  MOCK_METHOD(absl::optional<int>, MessageTooBigErrorCode, (),
              (const, override));
  MOCK_METHOD(QuicByteCount, GetMaxPacketSize,
              (const QuicSocketAddress& peer_address), (const, override));
  MOCK_METHOD(bool, SupportsReleaseTime, (), (const, override));
  MOCK_METHOD(bool, IsBatchMode, (), (const, override));
  MOCK_METHOD(QuicPacketBuffer, GetNextWriteLocation,
              (const QuicIpAddress& self_address,
               const QuicSocketAddress& peer_address),
              (override));
  MOCK_METHOD(WriteResult, Flush, (), (override));
};

class MockSendAlgorithm : public SendAlgorithmInterface {
 public:
  MockSendAlgorithm();
  MockSendAlgorithm(const MockSendAlgorithm&) = delete;
  MockSendAlgorithm& operator=(const MockSendAlgorithm&) = delete;
  ~MockSendAlgorithm() override;

  MOCK_METHOD(void, SetFromConfig,
              (const QuicConfig& config, Perspective perspective), (override));
  MOCK_METHOD(void, ApplyConnectionOptions,
              (const QuicTagVector& connection_options), (override));
  MOCK_METHOD(void, SetInitialCongestionWindowInPackets,
              (QuicPacketCount packets), (override));
  MOCK_METHOD(void, OnCongestionEvent,
              (bool rtt_updated, QuicByteCount bytes_in_flight,
               QuicTime event_time, const AckedPacketVector& acked_packets,
               const LostPacketVector& lost_packets),
              (override));
  MOCK_METHOD(void, OnPacketSent,
              (QuicTime, QuicByteCount, QuicPacketNumber, QuicByteCount,
               HasRetransmittableData),
              (override));
  MOCK_METHOD(void, OnPacketNeutered, (QuicPacketNumber), (override));
  MOCK_METHOD(void, OnRetransmissionTimeout, (bool), (override));
  MOCK_METHOD(void, OnConnectionMigration, (), (override));
  MOCK_METHOD(bool, CanSend, (QuicByteCount), (override));
  MOCK_METHOD(QuicBandwidth, PacingRate, (QuicByteCount), (const, override));
  MOCK_METHOD(QuicBandwidth, BandwidthEstimate, (), (const, override));
  MOCK_METHOD(bool, HasGoodBandwidthEstimateForResumption, (),
              (const, override));
  MOCK_METHOD(QuicByteCount, GetCongestionWindow, (), (const, override));
  MOCK_METHOD(std::string, GetDebugState, (), (const, override));
  MOCK_METHOD(bool, InSlowStart, (), (const, override));
  MOCK_METHOD(bool, InRecovery, (), (const, override));
  MOCK_METHOD(QuicByteCount, GetSlowStartThreshold, (), (const, override));
  MOCK_METHOD(CongestionControlType, GetCongestionControlType, (),
              (const, override));
  MOCK_METHOD(void, AdjustNetworkParameters, (const NetworkParams&),
              (override));
  MOCK_METHOD(void, OnApplicationLimited, (QuicByteCount), (override));
  MOCK_METHOD(void, PopulateConnectionStats, (QuicConnectionStats*),
              (const, override));
};

class MockLossAlgorithm : public LossDetectionInterface {
 public:
  MockLossAlgorithm();
  MockLossAlgorithm(const MockLossAlgorithm&) = delete;
  MockLossAlgorithm& operator=(const MockLossAlgorithm&) = delete;
  ~MockLossAlgorithm() override;

  MOCK_METHOD(void, SetFromConfig,
              (const QuicConfig& config, Perspective perspective), (override));

  MOCK_METHOD(DetectionStats, DetectLosses,
              (const QuicUnackedPacketMap& unacked_packets, QuicTime time,
               const RttStats& rtt_stats,
               QuicPacketNumber largest_recently_acked,
               const AckedPacketVector& packets_acked, LostPacketVector*),
              (override));
  MOCK_METHOD(QuicTime, GetLossTimeout, (), (const, override));
  MOCK_METHOD(void, SpuriousLossDetected,
              (const QuicUnackedPacketMap&, const RttStats&, QuicTime,
               QuicPacketNumber, QuicPacketNumber),
              (override));

  MOCK_METHOD(void, OnConfigNegotiated, (), (override));
  MOCK_METHOD(void, OnMinRttAvailable, (), (override));
  MOCK_METHOD(void, OnUserAgentIdKnown, (), (override));
  MOCK_METHOD(void, OnConnectionClosed, (), (override));
  MOCK_METHOD(void, OnReorderingDetected, (), (override));
};

class MockAckListener : public QuicAckListenerInterface {
 public:
  MockAckListener();
  MockAckListener(const MockAckListener&) = delete;
  MockAckListener& operator=(const MockAckListener&) = delete;

  MOCK_METHOD(void, OnPacketAcked,
              (int acked_bytes, QuicTime::Delta ack_delay_time), (override));

  MOCK_METHOD(void, OnPacketRetransmitted, (int retransmitted_bytes),
              (override));

 protected:
  // Object is ref counted.
  ~MockAckListener() override;
};

class MockNetworkChangeVisitor
    : public QuicSentPacketManager::NetworkChangeVisitor {
 public:
  MockNetworkChangeVisitor();
  MockNetworkChangeVisitor(const MockNetworkChangeVisitor&) = delete;
  MockNetworkChangeVisitor& operator=(const MockNetworkChangeVisitor&) = delete;
  ~MockNetworkChangeVisitor() override;

  MOCK_METHOD(void, OnCongestionChange, (), (override));
  MOCK_METHOD(void, OnPathMtuIncreased, (QuicPacketLength), (override));
};

class MockQuicConnectionDebugVisitor : public QuicConnectionDebugVisitor {
 public:
  MockQuicConnectionDebugVisitor();
  ~MockQuicConnectionDebugVisitor() override;

  MOCK_METHOD(void, OnPacketSent,
              (QuicPacketNumber, QuicPacketLength, bool, TransmissionType,
               EncryptionLevel, const QuicFrames&, const QuicFrames&, QuicTime),
              (override));

  MOCK_METHOD(void, OnCoalescedPacketSent, (const QuicCoalescedPacket&, size_t),
              (override));

  MOCK_METHOD(void, OnPingSent, (), (override));

  MOCK_METHOD(void, OnPacketReceived,
              (const QuicSocketAddress&, const QuicSocketAddress&,
               const QuicEncryptedPacket&),
              (override));

  MOCK_METHOD(void, OnIncorrectConnectionId, (QuicConnectionId), (override));

  MOCK_METHOD(void, OnProtocolVersionMismatch, (ParsedQuicVersion), (override));

  MOCK_METHOD(void, OnPacketHeader,
              (const QuicPacketHeader& header, QuicTime receive_time,
               EncryptionLevel level),
              (override));

  MOCK_METHOD(void, OnSuccessfulVersionNegotiation, (const ParsedQuicVersion&),
              (override));

  MOCK_METHOD(void, OnStreamFrame, (const QuicStreamFrame&), (override));

  MOCK_METHOD(void, OnCryptoFrame, (const QuicCryptoFrame&), (override));

  MOCK_METHOD(void, OnStopWaitingFrame, (const QuicStopWaitingFrame&),
              (override));

  MOCK_METHOD(void, OnRstStreamFrame, (const QuicRstStreamFrame&), (override));

  MOCK_METHOD(void, OnConnectionCloseFrame, (const QuicConnectionCloseFrame&),
              (override));

  MOCK_METHOD(void, OnBlockedFrame, (const QuicBlockedFrame&), (override));

  MOCK_METHOD(void, OnNewConnectionIdFrame, (const QuicNewConnectionIdFrame&),
              (override));

  MOCK_METHOD(void, OnRetireConnectionIdFrame,
              (const QuicRetireConnectionIdFrame&), (override));

  MOCK_METHOD(void, OnNewTokenFrame, (const QuicNewTokenFrame&), (override));

  MOCK_METHOD(void, OnMessageFrame, (const QuicMessageFrame&), (override));

  MOCK_METHOD(void, OnStopSendingFrame, (const QuicStopSendingFrame&),
              (override));

  MOCK_METHOD(void, OnPathChallengeFrame, (const QuicPathChallengeFrame&),
              (override));

  MOCK_METHOD(void, OnPathResponseFrame, (const QuicPathResponseFrame&),
              (override));

  MOCK_METHOD(void, OnPublicResetPacket, (const QuicPublicResetPacket&),
              (override));

  MOCK_METHOD(void, OnVersionNegotiationPacket,
              (const QuicVersionNegotiationPacket&), (override));

  MOCK_METHOD(void, OnTransportParametersSent, (const TransportParameters&),
              (override));

  MOCK_METHOD(void, OnTransportParametersReceived, (const TransportParameters&),
              (override));

  MOCK_METHOD(void, OnZeroRttRejected, (int), (override));
  MOCK_METHOD(void, OnZeroRttPacketAcked, (), (override));
};

class MockReceivedPacketManager : public QuicReceivedPacketManager {
 public:
  explicit MockReceivedPacketManager(QuicConnectionStats* stats);
  ~MockReceivedPacketManager() override;

  MOCK_METHOD(void, RecordPacketReceived,
              (const QuicPacketHeader& header, QuicTime receipt_time,
               const QuicEcnCodepoint ecn),
              (override));
  MOCK_METHOD(bool, IsMissing, (QuicPacketNumber packet_number), (override));
  MOCK_METHOD(bool, IsAwaitingPacket, (QuicPacketNumber packet_number),
              (const, override));
  MOCK_METHOD(bool, HasNewMissingPackets, (), (const, override));
  MOCK_METHOD(bool, ack_frame_updated, (), (const, override));
};

class MockPacketCreatorDelegate : public QuicPacketCreator::DelegateInterface {
 public:
  MockPacketCreatorDelegate();
  MockPacketCreatorDelegate(const MockPacketCreatorDelegate&) = delete;
  MockPacketCreatorDelegate& operator=(const MockPacketCreatorDelegate&) =
      delete;
  ~MockPacketCreatorDelegate() override;

  MOCK_METHOD(QuicPacketBuffer, GetPacketBuffer, (), (override));
  MOCK_METHOD(void, OnSerializedPacket, (SerializedPacket), (override));
  MOCK_METHOD(void, OnUnrecoverableError, (QuicErrorCode, const std::string&),
              (override));
  MOCK_METHOD(bool, ShouldGeneratePacket,
              (HasRetransmittableData retransmittable, IsHandshake handshake),
              (override));
  MOCK_METHOD(const QuicFrames, MaybeBundleAckOpportunistically, (),
              (override));
  MOCK_METHOD(SerializedPacketFate, GetSerializedPacketFate,
              (bool, EncryptionLevel), (override));
};

class MockSessionNotifier : public SessionNotifierInterface {
 public:
  MockSessionNotifier();
  ~MockSessionNotifier() override;

  MOCK_METHOD(bool, OnFrameAcked, (const QuicFrame&, QuicTime::Delta, QuicTime),
              (override));
  MOCK_METHOD(void, OnStreamFrameRetransmitted, (const QuicStreamFrame&),
              (override));
  MOCK_METHOD(void, OnFrameLost, (const QuicFrame&), (override));
  MOCK_METHOD(bool, RetransmitFrames,
              (const QuicFrames&, TransmissionType type), (override));
  MOCK_METHOD(bool, IsFrameOutstanding, (const QuicFrame&), (const, override));
  MOCK_METHOD(bool, HasUnackedCryptoData, (), (const, override));
  MOCK_METHOD(bool, HasUnackedStreamData, (), (const, override));
};

class MockQuicPathValidationContext : public QuicPathValidationContext {
 public:
  MockQuicPathValidationContext(const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address,
                                const QuicSocketAddress& effective_peer_address,
                                QuicPacketWriter* writer)
      : QuicPathValidationContext(self_address, peer_address,
                                  effective_peer_address),
        writer_(writer) {}
  QuicPacketWriter* WriterToUse() override { return writer_; }

 private:
  QuicPacketWriter* writer_;
};

class MockQuicPathValidationResultDelegate
    : public QuicPathValidator::ResultDelegate {
 public:
  MOCK_METHOD(void, OnPathValidationSuccess,
              (std::unique_ptr<QuicPathValidationContext>, QuicTime),
              (override));

  MOCK_METHOD(void, OnPathValidationFailure,
              (std::unique_ptr<QuicPathValidationContext>), (override));
};

class MockHttpDecoderVisitor : public HttpDecoder::Visitor {
 public:
  ~MockHttpDecoderVisitor() override = default;

  // Called if an error is detected.
  MOCK_METHOD(void, OnError, (HttpDecoder*), (override));

  MOCK_METHOD(bool, OnMaxPushIdFrame, (), (override));
  MOCK_METHOD(bool, OnGoAwayFrame, (const GoAwayFrame& frame), (override));
  MOCK_METHOD(bool, OnSettingsFrameStart, (QuicByteCount header_length),
              (override));
  MOCK_METHOD(bool, OnSettingsFrame, (const SettingsFrame& frame), (override));

  MOCK_METHOD(bool, OnDataFrameStart,
              (QuicByteCount header_length, QuicByteCount payload_length),
              (override));
  MOCK_METHOD(bool, OnDataFramePayload, (absl::string_view payload),
              (override));
  MOCK_METHOD(bool, OnDataFrameEnd, (), (override));

  MOCK_METHOD(bool, OnHeadersFrameStart,
              (QuicByteCount header_length, QuicByteCount payload_length),
              (override));
  MOCK_METHOD(bool, OnHeadersFramePayload, (absl::string_view payload),
              (override));
  MOCK_METHOD(bool, OnHeadersFrameEnd, (), (override));

  MOCK_METHOD(bool, OnPriorityUpdateFrameStart, (QuicByteCount header_length),
              (override));
  MOCK_METHOD(bool, OnPriorityUpdateFrame, (const PriorityUpdateFrame& frame),
              (override));

  MOCK_METHOD(bool, OnAcceptChFrameStart, (QuicByteCount header_length),
              (override));
  MOCK_METHOD(bool, OnAcceptChFrame, (const AcceptChFrame& frame), (override));
  MOCK_METHOD(void, OnWebTransportStreamFrameType,
              (QuicByteCount header_length, WebTransportSessionId session_id),
              (override));

  MOCK_METHOD(bool, OnUnknownFrameStart,
              (uint64_t frame_type, QuicByteCount header_length,
               QuicByteCount payload_length),
              (override));
  MOCK_METHOD(bool, OnUnknownFramePayload, (absl::string_view payload),
              (override));
  MOCK_METHOD(bool, OnUnknownFrameEnd, (), (override));
};

class QuicCryptoClientStreamPeer {
 public:
  QuicCryptoClientStreamPeer() = delete;

  static QuicCryptoClientStream::HandshakerInterface* GetHandshaker(
      QuicCryptoClientStream* stream);
};

// Creates a client session for testing.
//
// server_id: The server id associated with this stream.
// connection_start_time: The time to set for the connection clock.
//   Needed for strike-register nonce verification.  The client
//   connection_start_time should be synchronized witht the server
//   start time, otherwise nonce verification will fail.
// supported_versions: Set of QUIC versions this client supports.
// helper: Pointer to the MockQuicConnectionHelper to use for the session.
// crypto_client_config: Pointer to the crypto client config.
// client_connection: Pointer reference for newly created
//   connection.  This object will be owned by the
//   client_session.
// client_session: Pointer reference for the newly created client
//   session.  The new object will be owned by the caller.
void CreateClientSessionForTest(
    QuicServerId server_id, QuicTime::Delta connection_start_time,
    const ParsedQuicVersionVector& supported_versions,
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    QuicCryptoClientConfig* crypto_client_config,
    PacketSavingConnection** client_connection,
    TestQuicSpdyClientSession** client_session);

// Creates a server session for testing.
//
// server_id: The server id associated with this stream.
// connection_start_time: The time to set for the connection clock.
//   Needed for strike-register nonce verification.  The server
//   connection_start_time should be synchronized witht the client
//   start time, otherwise nonce verification will fail.
// supported_versions: Set of QUIC versions this server supports.
// helper: Pointer to the MockQuicConnectionHelper to use for the session.
// server_crypto_config: Pointer to the crypto server config.
// server_connection: Pointer reference for newly created
//   connection.  This object will be owned by the
//   server_session.
// server_session: Pointer reference for the newly created server
//   session.  The new object will be owned by the caller.
void CreateServerSessionForTest(
    QuicServerId server_id, QuicTime::Delta connection_start_time,
    ParsedQuicVersionVector supported_versions,
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    QuicCryptoServerConfig* server_crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    PacketSavingConnection** server_connection,
    TestQuicSpdyServerSession** server_session);

// Verifies that the relative error of |actual| with respect to |expected| is
// no more than |margin|.
// Please use EXPECT_APPROX_EQ, a wrapper around this function, for better error
// report.
template <typename T>
void ExpectApproxEq(T expected, T actual, float relative_margin) {
  // If |relative_margin| > 1 and T is an unsigned type, the comparison will
  // underflow.
  ASSERT_LE(relative_margin, 1);
  ASSERT_GE(relative_margin, 0);

  T absolute_margin = expected * relative_margin;

  EXPECT_GE(expected + absolute_margin, actual) << "actual value too big";
  EXPECT_LE(expected - absolute_margin, actual) << "actual value too small";
}

#define EXPECT_APPROX_EQ(expected, actual, relative_margin)                    \
  do {                                                                         \
    SCOPED_TRACE(testing::Message() << "relative_margin:" << relative_margin); \
    quic::test::ExpectApproxEq(expected, actual, relative_margin);             \
  } while (0)

template <typename T>
QuicHeaderList AsHeaderList(const T& container) {
  QuicHeaderList l;
  l.OnHeaderBlockStart();
  size_t total_size = 0;
  for (auto p : container) {
    total_size += p.first.size() + p.second.size();
    l.OnHeader(p.first, p.second);
  }
  l.OnHeaderBlockEnd(total_size, total_size);
  return l;
}

// Helper functions for stream ids, to allow test logic to abstract over the
// HTTP stream numbering scheme (i.e. whether one or two QUIC streams are used
// per HTTP transaction).
QuicStreamId GetNthClientInitiatedBidirectionalStreamId(
    QuicTransportVersion version, int n);
QuicStreamId GetNthServerInitiatedBidirectionalStreamId(
    QuicTransportVersion version, int n);
QuicStreamId GetNthServerInitiatedUnidirectionalStreamId(
    QuicTransportVersion version, int n);
QuicStreamId GetNthClientInitiatedUnidirectionalStreamId(
    QuicTransportVersion version, int n);

StreamType DetermineStreamType(QuicStreamId id, ParsedQuicVersion version,
                               Perspective perspective, bool is_incoming,
                               StreamType default_type);

// Creates a MemSlice using a singleton trivial buffer allocator.  Performs a
// copy.
quiche::QuicheMemSlice MemSliceFromString(absl::string_view data);

// Used to compare ReceivedPacketInfo.
MATCHER_P(ReceivedPacketInfoEquals, info, "") {
  return info.ToString() == arg.ToString();
}

MATCHER_P(ReceivedPacketInfoConnectionIdEquals, destination_connection_id, "") {
  return arg.destination_connection_id == destination_connection_id;
}

MATCHER_P2(InRange, min, max, "") { return arg >= min && arg <= max; }

// A GMock matcher that prints expected and actual QuicErrorCode strings
// upon failure.  Example usage:
// EXPECT_THAT(stream_->connection_error(), IsError(QUIC_INTERNAL_ERROR));
MATCHER_P(IsError, expected,
          absl::StrCat(negation ? "isn't equal to " : "is equal to ",
                       QuicErrorCodeToString(expected))) {
  *result_listener << QuicErrorCodeToString(static_cast<QuicErrorCode>(arg));
  return arg == expected;
}

// Shorthand for IsError(QUIC_NO_ERROR).
// Example usage: EXPECT_THAT(stream_->connection_error(), IsQuicNoError());
MATCHER(IsQuicNoError,
        absl::StrCat(negation ? "isn't equal to " : "is equal to ",
                     QuicErrorCodeToString(QUIC_NO_ERROR))) {
  *result_listener << QuicErrorCodeToString(arg);
  return arg == QUIC_NO_ERROR;
}

// A GMock matcher that prints expected and actual QuicRstStreamErrorCode
// strings upon failure.  Example usage:
// EXPECT_THAT(stream_->stream_error(), IsStreamError(QUIC_INTERNAL_ERROR));
MATCHER_P(IsStreamError, expected,
          absl::StrCat(negation ? "isn't equal to " : "is equal to ",
                       QuicRstStreamErrorCodeToString(expected))) {
  *result_listener << QuicRstStreamErrorCodeToString(arg);
  return arg == expected;
}

// Shorthand for IsStreamError(QUIC_STREAM_NO_ERROR).  Example usage:
// EXPECT_THAT(stream_->stream_error(), IsQuicStreamNoError());
MATCHER(IsQuicStreamNoError,
        absl::StrCat(negation ? "isn't equal to " : "is equal to ",
                     QuicRstStreamErrorCodeToString(QUIC_STREAM_NO_ERROR))) {
  *result_listener << QuicRstStreamErrorCodeToString(arg);
  return arg == QUIC_STREAM_NO_ERROR;
}

// TaggingEncrypter appends kTagSize bytes of |tag| to the end of each message.
class TaggingEncrypter : public QuicEncrypter {
 public:
  explicit TaggingEncrypter(uint8_t tag) : tag_(tag) {}
  TaggingEncrypter(const TaggingEncrypter&) = delete;
  TaggingEncrypter& operator=(const TaggingEncrypter&) = delete;

  ~TaggingEncrypter() override {}

  // QuicEncrypter interface.
  bool SetKey(absl::string_view /*key*/) override { return true; }

  bool SetNoncePrefix(absl::string_view /*nonce_prefix*/) override {
    return true;
  }

  bool SetIV(absl::string_view /*iv*/) override { return true; }

  bool SetHeaderProtectionKey(absl::string_view /*key*/) override {
    return true;
  }

  bool EncryptPacket(uint64_t packet_number, absl::string_view associated_data,
                     absl::string_view plaintext, char* output,
                     size_t* output_length, size_t max_output_length) override;

  std::string GenerateHeaderProtectionMask(
      absl::string_view /*sample*/) override {
    return std::string(5, 0);
  }

  size_t GetKeySize() const override { return 0; }
  size_t GetNoncePrefixSize() const override { return 0; }
  size_t GetIVSize() const override { return 0; }

  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override {
    return ciphertext_size - kTagSize;
  }

  size_t GetCiphertextSize(size_t plaintext_size) const override {
    return plaintext_size + kTagSize;
  }

  QuicPacketCount GetConfidentialityLimit() const override {
    return std::numeric_limits<QuicPacketCount>::max();
  }

  absl::string_view GetKey() const override { return absl::string_view(); }

  absl::string_view GetNoncePrefix() const override {
    return absl::string_view();
  }

 private:
  enum {
    kTagSize = 16,
  };

  const uint8_t tag_;
};

// TaggingDecrypter ensures that the final kTagSize bytes of the message all
// have the same value and then removes them.
class TaggingDecrypter : public QuicDecrypter {
 public:
  ~TaggingDecrypter() override {}

  // QuicDecrypter interface
  bool SetKey(absl::string_view /*key*/) override { return true; }

  bool SetNoncePrefix(absl::string_view /*nonce_prefix*/) override {
    return true;
  }

  bool SetIV(absl::string_view /*iv*/) override { return true; }

  bool SetHeaderProtectionKey(absl::string_view /*key*/) override {
    return true;
  }

  bool SetPreliminaryKey(absl::string_view /*key*/) override {
    QUIC_BUG(quic_bug_10230_1) << "should not be called";
    return false;
  }

  bool SetDiversificationNonce(const DiversificationNonce& /*key*/) override {
    return true;
  }

  bool DecryptPacket(uint64_t packet_number, absl::string_view associated_data,
                     absl::string_view ciphertext, char* output,
                     size_t* output_length, size_t max_output_length) override;

  std::string GenerateHeaderProtectionMask(
      QuicDataReader* /*sample_reader*/) override {
    return std::string(5, 0);
  }

  size_t GetKeySize() const override { return 0; }
  size_t GetNoncePrefixSize() const override { return 0; }
  size_t GetIVSize() const override { return 0; }
  absl::string_view GetKey() const override { return absl::string_view(); }
  absl::string_view GetNoncePrefix() const override {
    return absl::string_view();
  }
  // Use a distinct value starting with 0xFFFFFF, which is never used by TLS.
  uint32_t cipher_id() const override { return 0xFFFFFFF0; }
  QuicPacketCount GetIntegrityLimit() const override {
    return std::numeric_limits<QuicPacketCount>::max();
  }

 protected:
  virtual uint8_t GetTag(absl::string_view ciphertext) {
    return ciphertext.data()[ciphertext.size() - 1];
  }

 private:
  enum {
    kTagSize = 16,
  };

  bool CheckTag(absl::string_view ciphertext, uint8_t tag);
};

// StringTaggingDecrypter ensures that the final kTagSize bytes of the message
// match the expected value.
class StrictTaggingDecrypter : public TaggingDecrypter {
 public:
  explicit StrictTaggingDecrypter(uint8_t tag) : tag_(tag) {}
  ~StrictTaggingDecrypter() override {}

  // TaggingQuicDecrypter
  uint8_t GetTag(absl::string_view /*ciphertext*/) override { return tag_; }

  // Use a distinct value starting with 0xFFFFFF, which is never used by TLS.
  uint32_t cipher_id() const override { return 0xFFFFFFF1; }

 private:
  const uint8_t tag_;
};

class TestPacketWriter : public QuicPacketWriter {
  struct PacketBuffer {
    ABSL_CACHELINE_ALIGNED char buffer[1500];
    bool in_use = false;
  };

 public:
  TestPacketWriter(ParsedQuicVersion version, MockClock* clock,
                   Perspective perspective);

  TestPacketWriter(const TestPacketWriter&) = delete;
  TestPacketWriter& operator=(const TestPacketWriter&) = delete;

  ~TestPacketWriter() override;

  // QuicPacketWriter interface
  WriteResult WritePacket(const char* buffer, size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;

  bool ShouldWriteFail() { return write_should_fail_; }

  bool IsWriteBlocked() const override { return write_blocked_; }

  absl::optional<int> MessageTooBigErrorCode() const override { return 0x1234; }

  void SetWriteBlocked() { write_blocked_ = true; }

  void SetWritable() override { write_blocked_ = false; }

  void SetShouldWriteFail() { write_should_fail_ = true; }

  void SetWriteError(int error_code) { write_error_code_ = error_code; }

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& /*peer_address*/) const override {
    return max_packet_size_;
  }

  bool SupportsReleaseTime() const override { return supports_release_time_; }

  bool IsBatchMode() const override { return is_batch_mode_; }

  QuicPacketBuffer GetNextWriteLocation(
      const QuicIpAddress& /*self_address*/,
      const QuicSocketAddress& /*peer_address*/) override;

  WriteResult Flush() override;

  void BlockOnNextFlush() { block_on_next_flush_ = true; }

  void BlockOnNextWrite() { block_on_next_write_ = true; }

  void SimulateNextPacketTooLarge() { next_packet_too_large_ = true; }

  void ExpectNextPacketUnprocessable() { next_packet_processable_ = false; }

  void AlwaysGetPacketTooLarge() { always_get_packet_too_large_ = true; }

  // Sets the amount of time that the writer should before the actual write.
  void SetWritePauseTimeDelta(QuicTime::Delta delta) {
    write_pause_time_delta_ = delta;
  }

  void SetBatchMode(bool new_value) { is_batch_mode_ = new_value; }

  const QuicPacketHeader& header() { return framer_.header(); }

  size_t frame_count() const { return framer_.num_frames(); }

  const std::vector<QuicAckFrame>& ack_frames() const {
    return framer_.ack_frames();
  }

  const std::vector<QuicStopWaitingFrame>& stop_waiting_frames() const {
    return framer_.stop_waiting_frames();
  }

  const std::vector<QuicConnectionCloseFrame>& connection_close_frames() const {
    return framer_.connection_close_frames();
  }

  const std::vector<QuicRstStreamFrame>& rst_stream_frames() const {
    return framer_.rst_stream_frames();
  }

  const std::vector<std::unique_ptr<QuicStreamFrame>>& stream_frames() const {
    return framer_.stream_frames();
  }

  const std::vector<std::unique_ptr<QuicCryptoFrame>>& crypto_frames() const {
    return framer_.crypto_frames();
  }

  const std::vector<QuicPingFrame>& ping_frames() const {
    return framer_.ping_frames();
  }

  const std::vector<QuicMessageFrame>& message_frames() const {
    return framer_.message_frames();
  }

  const std::vector<QuicWindowUpdateFrame>& window_update_frames() const {
    return framer_.window_update_frames();
  }

  const std::vector<QuicPaddingFrame>& padding_frames() const {
    return framer_.padding_frames();
  }

  const std::vector<QuicPathChallengeFrame>& path_challenge_frames() const {
    return framer_.path_challenge_frames();
  }

  const std::vector<QuicPathResponseFrame>& path_response_frames() const {
    return framer_.path_response_frames();
  }

  const QuicEncryptedPacket* coalesced_packet() const {
    return framer_.coalesced_packet();
  }

  size_t last_packet_size() const { return last_packet_size_; }

  size_t total_bytes_written() const { return total_bytes_written_; }

  const QuicPacketHeader& last_packet_header() const {
    return last_packet_header_;
  }

  const QuicVersionNegotiationPacket* version_negotiation_packet() {
    return framer_.version_negotiation_packet();
  }

  void set_is_write_blocked_data_buffered(bool buffered) {
    is_write_blocked_data_buffered_ = buffered;
  }

  void set_perspective(Perspective perspective) {
    // We invert perspective here, because the framer needs to parse packets
    // we send.
    QuicFramerPeer::SetPerspective(framer_.framer(),
                                   QuicUtils::InvertPerspective(perspective));
    framer_.framer()->SetInitialObfuscators(TestConnectionId());
  }

  // final_bytes_of_last_packet_ returns the last four bytes of the previous
  // packet as a little-endian, uint32_t. This is intended to be used with a
  // TaggingEncrypter so that tests can determine which encrypter was used for
  // a given packet.
  uint32_t final_bytes_of_last_packet() { return final_bytes_of_last_packet_; }

  // Returns the final bytes of the second to last packet.
  uint32_t final_bytes_of_previous_packet() {
    return final_bytes_of_previous_packet_;
  }

  uint32_t packets_write_attempts() const { return packets_write_attempts_; }

  uint32_t flush_attempts() const { return flush_attempts_; }

  uint32_t connection_close_packets() const {
    return connection_close_packets_;
  }

  void Reset() { framer_.Reset(); }

  void SetSupportedVersions(const ParsedQuicVersionVector& versions) {
    framer_.SetSupportedVersions(versions);
  }

  void set_max_packet_size(QuicByteCount max_packet_size) {
    max_packet_size_ = max_packet_size;
  }

  void set_supports_release_time(bool supports_release_time) {
    supports_release_time_ = supports_release_time;
  }

  SimpleQuicFramer* framer() { return &framer_; }

  const QuicIpAddress& last_write_source_address() const {
    return last_write_source_address_;
  }

  const QuicSocketAddress& last_write_peer_address() const {
    return last_write_peer_address_;
  }

 private:
  char* AllocPacketBuffer();

  void FreePacketBuffer(const char* buffer);

  ParsedQuicVersion version_;
  SimpleQuicFramer framer_;
  size_t last_packet_size_ = 0;
  size_t total_bytes_written_ = 0;
  QuicPacketHeader last_packet_header_;
  bool write_blocked_ = false;
  bool write_should_fail_ = false;
  bool block_on_next_flush_ = false;
  bool block_on_next_write_ = false;
  bool next_packet_too_large_ = false;
  bool next_packet_processable_ = true;
  bool always_get_packet_too_large_ = false;
  bool is_write_blocked_data_buffered_ = false;
  bool is_batch_mode_ = false;
  // Number of times Flush() was called.
  uint32_t flush_attempts_ = 0;
  // (Batch mode only) Number of bytes buffered in writer. It is used as the
  // return value of a successful Flush().
  uint32_t bytes_buffered_ = 0;
  uint32_t final_bytes_of_last_packet_ = 0;
  uint32_t final_bytes_of_previous_packet_ = 0;
  uint32_t packets_write_attempts_ = 0;
  uint32_t connection_close_packets_ = 0;
  MockClock* clock_ = nullptr;
  // If non-zero, the clock will pause during WritePacket for this amount of
  // time.
  QuicTime::Delta write_pause_time_delta_ = QuicTime::Delta::Zero();
  QuicByteCount max_packet_size_ = kMaxOutgoingPacketSize;
  bool supports_release_time_ = false;
  // Used to verify writer-allocated packet buffers are properly released.
  std::vector<PacketBuffer*> packet_buffer_pool_;
  // Buffer address => Address of the owning PacketBuffer.
  absl::flat_hash_map<char*, PacketBuffer*, absl::Hash<char*>>
      packet_buffer_pool_index_;
  // Indices in packet_buffer_pool_ that are not allocated.
  std::list<PacketBuffer*> packet_buffer_free_list_;
  // The soruce/peer address passed into WritePacket().
  QuicIpAddress last_write_source_address_;
  QuicSocketAddress last_write_peer_address_;
  int write_error_code_{0};
};

// Parses a packet generated by
// QuicFramer::WriteClientVersionNegotiationProbePacket.
// |packet_bytes| must point to |packet_length| bytes in memory which represent
// the packet. This method will fill in |destination_connection_id_bytes|
// which must point to at least |*destination_connection_id_length_out| bytes in
// memory. |*destination_connection_id_length_out| will contain the length of
// the received destination connection ID, which on success will match the
// contents of the destination connection ID passed in to
// WriteClientVersionNegotiationProbePacket.
bool ParseClientVersionNegotiationProbePacket(
    const char* packet_bytes, size_t packet_length,
    char* destination_connection_id_bytes,
    uint8_t* destination_connection_id_length_out);

// Writes an array of bytes that correspond to a QUIC version negotiation packet
// that a QUIC server would send in response to a probe created by
// QuicFramer::WriteClientVersionNegotiationProbePacket.
// The bytes will be written to |packet_bytes|, which must point to
// |*packet_length_out| bytes of memory. |*packet_length_out| will contain the
// length of the created packet. |source_connection_id_bytes| will be sent as
// the source connection ID, and must point to |source_connection_id_length|
// bytes of memory.
bool WriteServerVersionNegotiationProbeResponse(
    char* packet_bytes, size_t* packet_length_out,
    const char* source_connection_id_bytes,
    uint8_t source_connection_id_length);

// Implementation of Http3DatagramVisitor which saves all received datagrams.
class SavingHttp3DatagramVisitor : public QuicSpdyStream::Http3DatagramVisitor {
 public:
  struct SavedHttp3Datagram {
    QuicStreamId stream_id;
    std::string payload;
    bool operator==(const SavedHttp3Datagram& o) const {
      return stream_id == o.stream_id && payload == o.payload;
    }
  };
  const std::vector<SavedHttp3Datagram>& received_h3_datagrams() const {
    return received_h3_datagrams_;
  }

  // Override from QuicSpdyStream::Http3DatagramVisitor.
  void OnHttp3Datagram(QuicStreamId stream_id,
                       absl::string_view payload) override {
    received_h3_datagrams_.push_back(
        SavedHttp3Datagram{stream_id, std::string(payload)});
  }

 private:
  std::vector<SavedHttp3Datagram> received_h3_datagrams_;
};

// Implementation of ConnectIpVisitor which saves all received capsules.
class SavingConnectIpVisitor : public QuicSpdyStream::ConnectIpVisitor {
 public:
  const std::vector<quiche::AddressAssignCapsule>&
  received_address_assign_capsules() const {
    return received_address_assign_capsules_;
  }
  const std::vector<quiche::AddressRequestCapsule>&
  received_address_request_capsules() const {
    return received_address_request_capsules_;
  }
  const std::vector<quiche::RouteAdvertisementCapsule>&
  received_route_advertisement_capsules() const {
    return received_route_advertisement_capsules_;
  }
  bool headers_written() const { return headers_written_; }

  // From QuicSpdyStream::ConnectIpVisitor.
  bool OnAddressAssignCapsule(
      const quiche::AddressAssignCapsule& capsule) override {
    received_address_assign_capsules_.push_back(capsule);
    return true;
  }
  bool OnAddressRequestCapsule(
      const quiche::AddressRequestCapsule& capsule) override {
    received_address_request_capsules_.push_back(capsule);
    return true;
  }
  bool OnRouteAdvertisementCapsule(
      const quiche::RouteAdvertisementCapsule& capsule) override {
    received_route_advertisement_capsules_.push_back(capsule);
    return true;
  }
  void OnHeadersWritten() override { headers_written_ = true; }

 private:
  std::vector<quiche::AddressAssignCapsule> received_address_assign_capsules_;
  std::vector<quiche::AddressRequestCapsule> received_address_request_capsules_;
  std::vector<quiche::RouteAdvertisementCapsule>
      received_route_advertisement_capsules_;
  bool headers_written_ = false;
};

inline std::string EscapeTestParamName(absl::string_view name) {
  std::string result(name);
  // Escape all characters that are not allowed by gtest ([a-zA-Z0-9_]).
  for (char& c : result) {
    bool valid = absl::ascii_isalnum(c) || c == '_';
    if (!valid) {
      c = '_';
    }
  }
  return result;
}

struct TestPerPacketOptions : PerPacketOptions {
 public:
  std::unique_ptr<quic::PerPacketOptions> Clone() const override {
    return std::make_unique<TestPerPacketOptions>(*this);
  }
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_UTILS_H_
