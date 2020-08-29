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

#include "net/third_party/quiche/src/quic/core/congestion_control/loss_detection_interface.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"
#include "net/third_party/quiche/src/quic/core/http/quic_client_push_promise_index.h"
#include "net/third_party/quiche/src/quic/core/http/quic_server_session_base.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_sent_packet_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_storage.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_quic_session_visitor.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_random.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

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
  kInitialStreamFlowControlWindowForTest = 1024 * 1024,   // 1 MB
  kInitialSessionFlowControlWindowForTest = 1536 * 1024,  // 1.5 MB
};

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
    Perspective perspective);

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
    ParsedQuicVersionVector* versions);

// Create an encrypted packet for testing.
// If versions == nullptr, uses &AllSupportedVersions().
// Note that the packet is encrypted with NullEncrypter, so to decrypt the
// constructed packet, the framer must be set to use NullDecrypter.
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
    ParsedQuicVersionVector* versions);

// This form assumes |versions| == nullptr.
QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data,
    QuicConnectionIdIncluded destination_connection_id_included,
    QuicConnectionIdIncluded source_connection_id_included,
    QuicPacketNumberLength packet_number_length);

// This form assumes |connection_id_length| == PACKET_8BYTE_CONNECTION_ID,
// |packet_number_length| == PACKET_4BYTE_PACKET_NUMBER and
// |versions| == nullptr.
QuicEncryptedPacket* ConstructEncryptedPacket(
    QuicConnectionId destination_connection_id,
    QuicConnectionId source_connection_id,
    bool version_flag,
    bool reset_flag,
    uint64_t packet_number,
    const std::string& data);

// Creates a client-to-server ZERO-RTT packet that will fail to decrypt.
std::unique_ptr<QuicEncryptedPacket> GetUndecryptableEarlyPacket(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id);

// Constructs a received packet for testing. The caller must take ownership
// of the returned pointer.
QuicReceivedPacket* ConstructReceivedPacket(
    const QuicEncryptedPacket& encrypted_packet,
    QuicTime receipt_time);

// Create an encrypted packet for testing whose data portion erroneous.
// The specific way the data portion is erroneous is not specified, but
// it is an error that QuicFramer detects.
// Note that the packet is encrypted with NullEncrypter, so to decrypt the
// constructed packet, the framer must be set to use NullDecrypter.
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
QuicAckFrame MakeAckFrameWithGaps(uint64_t gap_size,
                                  size_t max_num_gaps,
                                  uint64_t largest_acked);

// Returns the encryption level that corresponds to the header type in
// |header|. If the header is for GOOGLE_QUIC_PACKET instead of an
// IETF-invariants packet, this function returns ENCRYPTION_INITIAL.
EncryptionLevel HeaderToEncryptionLevel(const QuicPacketHeader& header);

// Returns a QuicPacket that is owned by the caller, and
// is populated with the fields in |header| and |frames|, or is nullptr if the
// packet could not be created.
std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer,
    const QuicPacketHeader& header,
    const QuicFrames& frames);
// Returns a QuicPacket that is owned by the caller, and of size |packet_size|.
std::unique_ptr<QuicPacket> BuildUnsizedDataPacket(
    QuicFramer* framer,
    const QuicPacketHeader& header,
    const QuicFrames& frames,
    size_t packet_size);

// Compute SHA-1 hash of the supplied std::string.
std::string Sha1Hash(quiche::QuicheStringPiece data);

// Delete |frame| and return true.
bool ClearControlFrame(const QuicFrame& frame);

// Simple random number generator used to compute random numbers suitable
// for pseudo-randomly dropping packets in tests.
class SimpleRandom : public QuicRandom {
 public:
  SimpleRandom() { set_seed(0); }
  SimpleRandom(const SimpleRandom&) = delete;
  SimpleRandom& operator=(const SimpleRandom&) = delete;
  ~SimpleRandom() override {}

  // Returns a random number in the range [0, kuint64max].
  uint64_t RandUint64() override;

  void RandBytes(void* data, size_t len) override;

  void set_seed(uint64_t seed);

 private:
  uint8_t buffer_[4096];
  size_t buffer_offset_;
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
  MOCK_METHOD(bool,
              OnProtocolVersionMismatch,
              (ParsedQuicVersion version),
              (override));
  MOCK_METHOD(void, OnPacket, (), (override));
  MOCK_METHOD(void,
              OnPublicResetPacket,
              (const QuicPublicResetPacket& header),
              (override));
  MOCK_METHOD(void,
              OnVersionNegotiationPacket,
              (const QuicVersionNegotiationPacket& packet),
              (override));
  MOCK_METHOD(void,
              OnRetryPacket,
              (QuicConnectionId original_connection_id,
               QuicConnectionId new_connection_id,
               quiche::QuicheStringPiece retry_token,
               quiche::QuicheStringPiece retry_integrity_tag,
               quiche::QuicheStringPiece retry_without_tag),
              (override));
  // The constructor sets this up to return true by default.
  MOCK_METHOD(bool,
              OnUnauthenticatedHeader,
              (const QuicPacketHeader& header),
              (override));
  // The constructor sets this up to return true by default.
  MOCK_METHOD(bool,
              OnUnauthenticatedPublicHeader,
              (const QuicPacketHeader& header),
              (override));
  MOCK_METHOD(void, OnDecryptedPacket, (EncryptionLevel level), (override));
  MOCK_METHOD(bool,
              OnPacketHeader,
              (const QuicPacketHeader& header),
              (override));
  MOCK_METHOD(void,
              OnCoalescedPacket,
              (const QuicEncryptedPacket& packet),
              (override));
  MOCK_METHOD(void,
              OnUndecryptablePacket,
              (const QuicEncryptedPacket& packet,
               EncryptionLevel decryption_level,
               bool has_decryption_key),
              (override));
  MOCK_METHOD(bool, OnStreamFrame, (const QuicStreamFrame& frame), (override));
  MOCK_METHOD(bool, OnCryptoFrame, (const QuicCryptoFrame& frame), (override));
  MOCK_METHOD(bool,
              OnAckFrameStart,
              (QuicPacketNumber, QuicTime::Delta),
              (override));
  MOCK_METHOD(bool,
              OnAckRange,
              (QuicPacketNumber, QuicPacketNumber),
              (override));
  MOCK_METHOD(bool, OnAckTimestamp, (QuicPacketNumber, QuicTime), (override));
  MOCK_METHOD(bool, OnAckFrameEnd, (QuicPacketNumber), (override));
  MOCK_METHOD(bool,
              OnStopWaitingFrame,
              (const QuicStopWaitingFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnPaddingFrame,
              (const QuicPaddingFrame& frame),
              (override));
  MOCK_METHOD(bool, OnPingFrame, (const QuicPingFrame& frame), (override));
  MOCK_METHOD(bool,
              OnRstStreamFrame,
              (const QuicRstStreamFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnConnectionCloseFrame,
              (const QuicConnectionCloseFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnNewConnectionIdFrame,
              (const QuicNewConnectionIdFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnRetireConnectionIdFrame,
              (const QuicRetireConnectionIdFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnNewTokenFrame,
              (const QuicNewTokenFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnStopSendingFrame,
              (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnPathChallengeFrame,
              (const QuicPathChallengeFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnPathResponseFrame,
              (const QuicPathResponseFrame& frame),
              (override));
  MOCK_METHOD(bool, OnGoAwayFrame, (const QuicGoAwayFrame& frame), (override));
  MOCK_METHOD(bool,
              OnMaxStreamsFrame,
              (const QuicMaxStreamsFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnStreamsBlockedFrame,
              (const QuicStreamsBlockedFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnWindowUpdateFrame,
              (const QuicWindowUpdateFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnBlockedFrame,
              (const QuicBlockedFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnMessageFrame,
              (const QuicMessageFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnHandshakeDoneFrame,
              (const QuicHandshakeDoneFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnAckFrequencyFrame,
              (const QuicAckFrequencyFrame& frame),
              (override));
  MOCK_METHOD(void, OnPacketComplete, (), (override));
  MOCK_METHOD(bool,
              IsValidStatelessResetToken,
              (QuicUint128),
              (const, override));
  MOCK_METHOD(void,
              OnAuthenticatedIetfStatelessResetPacket,
              (const QuicIetfStatelessResetPacket&),
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
                     quiche::QuicheStringPiece /*retry_token*/,
                     quiche::QuicheStringPiece /*retry_integrity_tag*/,
                     quiche::QuicheStringPiece /*retry_without_tag*/) override {
  }
  bool OnProtocolVersionMismatch(ParsedQuicVersion version) override;
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override;
  bool OnUnauthenticatedPublicHeader(const QuicPacketHeader& header) override;
  void OnDecryptedPacket(EncryptionLevel /*level*/) override {}
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
  bool IsValidStatelessResetToken(QuicUint128 token) const override;
  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& /*packet*/) override {}
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
  MOCK_METHOD(void,
              OnWindowUpdateFrame,
              (const QuicWindowUpdateFrame& frame),
              (override));
  MOCK_METHOD(void,
              OnBlockedFrame,
              (const QuicBlockedFrame& frame),
              (override));
  MOCK_METHOD(void, OnRstStream, (const QuicRstStreamFrame& frame), (override));
  MOCK_METHOD(void, OnGoAway, (const QuicGoAwayFrame& frame), (override));
  MOCK_METHOD(void,
              OnMessageReceived,
              (quiche::QuicheStringPiece message),
              (override));
  MOCK_METHOD(void, OnHandshakeDoneReceived, (), (override));
  MOCK_METHOD(void,
              OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(void, OnWriteBlocked, (), (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
  MOCK_METHOD(bool, SendProbingData, (), (override));
  MOCK_METHOD(bool,
              ValidateStatelessReset,
              (const quic::QuicSocketAddress&, const quic::QuicSocketAddress&),
              (override));
  MOCK_METHOD(void, OnCongestionWindowChange, (QuicTime now), (override));
  MOCK_METHOD(void,
              OnConnectionMigration,
              (AddressChangeType type),
              (override));
  MOCK_METHOD(void, OnPathDegrading, (), (override));
  MOCK_METHOD(void, OnForwardProgressMadeAfterPathDegrading, (), (override));
  MOCK_METHOD(bool, WillingAndAbleToWrite, (), (const, override));
  MOCK_METHOD(bool, ShouldKeepConnectionAlive, (), (const, override));
  MOCK_METHOD(void,
              OnSuccessfulVersionNegotiation,
              (const ParsedQuicVersion& version),
              (override));
  MOCK_METHOD(void,
              OnPacketReceived,
              (const QuicSocketAddress& self_address,
               const QuicSocketAddress& peer_address,
               bool is_connectivity_probe),
              (override));
  MOCK_METHOD(void, OnAckNeedsRetransmittableFrame, (), (override));
  MOCK_METHOD(void, SendPing, (), (override));
  MOCK_METHOD(bool, AllowSelfAddressChange, (), (const, override));
  MOCK_METHOD(HandshakeState, GetHandshakeState, (), (const, override));
  MOCK_METHOD(bool,
              OnMaxStreamsFrame,
              (const QuicMaxStreamsFrame& frame),
              (override));
  MOCK_METHOD(bool,
              OnStreamsBlockedFrame,
              (const QuicStreamsBlockedFrame& frame),
              (override));
  MOCK_METHOD(void,
              OnStopSendingFrame,
              (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(void, OnPacketDecrypted, (EncryptionLevel), (override));
  MOCK_METHOD(void, OnOneRttPacketAcknowledged, (), (override));
  MOCK_METHOD(void, OnHandshakePacketSent, (), (override));
};

class MockQuicConnectionHelper : public QuicConnectionHelperInterface {
 public:
  MockQuicConnectionHelper();
  MockQuicConnectionHelper(const MockQuicConnectionHelper&) = delete;
  MockQuicConnectionHelper& operator=(const MockQuicConnectionHelper&) = delete;
  ~MockQuicConnectionHelper() override;
  const QuicClock* GetClock() const override;
  QuicRandom* GetRandomGenerator() override;
  QuicBufferAllocator* GetStreamSendBufferAllocator() override;
  void AdvanceTime(QuicTime::Delta delta);

 private:
  MockClock clock_;
  MockRandom random_generator_;
  SimpleBufferAllocator buffer_allocator_;
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

class MockQuicConnection : public QuicConnection {
 public:
  // Uses a ConnectionId of 42 and 127.0.0.1:123.
  MockQuicConnection(MockQuicConnectionHelper* helper,
                     MockAlarmFactory* alarm_factory,
                     Perspective perspective);

  // Uses a ConnectionId of 42.
  MockQuicConnection(QuicSocketAddress address,
                     MockQuicConnectionHelper* helper,
                     MockAlarmFactory* alarm_factory,
                     Perspective perspective);

  // Uses 127.0.0.1:123.
  MockQuicConnection(QuicConnectionId connection_id,
                     MockQuicConnectionHelper* helper,
                     MockAlarmFactory* alarm_factory,
                     Perspective perspective);

  // Uses a ConnectionId of 42, and 127.0.0.1:123.
  MockQuicConnection(MockQuicConnectionHelper* helper,
                     MockAlarmFactory* alarm_factory,
                     Perspective perspective,
                     const ParsedQuicVersionVector& supported_versions);

  MockQuicConnection(QuicConnectionId connection_id,
                     QuicSocketAddress address,
                     MockQuicConnectionHelper* helper,
                     MockAlarmFactory* alarm_factory,
                     Perspective perspective,
                     const ParsedQuicVersionVector& supported_versions);
  MockQuicConnection(const MockQuicConnection&) = delete;
  MockQuicConnection& operator=(const MockQuicConnection&) = delete;

  ~MockQuicConnection() override;

  // If the constructor that uses a MockQuicConnectionHelper has been used then
  // this method
  // will advance the time of the MockClock.
  void AdvanceTime(QuicTime::Delta delta);

  MOCK_METHOD(void,
              ProcessUdpPacket,
              (const QuicSocketAddress& self_address,
               const QuicSocketAddress& peer_address,
               const QuicReceivedPacket& packet),
              (override));
  MOCK_METHOD(void,
              CloseConnection,
              (QuicErrorCode error,
               const std::string& details,
               ConnectionCloseBehavior connection_close_behavior),
              (override));
  MOCK_METHOD(void,
              SendConnectionClosePacket,
              (QuicErrorCode error, const std::string& details),
              (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
  MOCK_METHOD(void,
              SendConnectivityProbingResponsePacket,
              (const QuicSocketAddress& peer_address),
              (override));
  MOCK_METHOD(bool,
              SendConnectivityProbingPacket,
              (QuicPacketWriter*, const QuicSocketAddress& peer_address),
              (override));

  MOCK_METHOD(void,
              OnSendConnectionState,
              (const CachedNetworkParameters&),
              (override));
  MOCK_METHOD(void,
              ResumeConnectionState,
              (const CachedNetworkParameters&, bool),
              (override));
  MOCK_METHOD(void, SetMaxPacingRate, (QuicBandwidth), (override));

  MOCK_METHOD(void,
              OnStreamReset,
              (QuicStreamId, QuicRstStreamErrorCode),
              (override));
  MOCK_METHOD(bool, SendControlFrame, (const QuicFrame& frame), (override));
  MOCK_METHOD(MessageStatus,
              SendMessage,
              (QuicMessageId, QuicMemSliceSpan, bool),
              (override));

  MOCK_METHOD(void, OnError, (QuicFramer*), (override));
  void QuicConnection_OnError(QuicFramer* framer) {
    QuicConnection::OnError(framer);
  }

  void ReallyOnCanWrite() { QuicConnection::OnCanWrite(); }

  void ReallyCloseConnection(
      QuicErrorCode error,
      const std::string& details,
      ConnectionCloseBehavior connection_close_behavior) {
    QuicConnection::CloseConnection(error, details, connection_close_behavior);
  }

  void ReallyProcessUdpPacket(const QuicSocketAddress& self_address,
                              const QuicSocketAddress& peer_address,
                              const QuicReceivedPacket& packet) {
    QuicConnection::ProcessUdpPacket(self_address, peer_address, packet);
  }

  bool OnProtocolVersionMismatch(ParsedQuicVersion version) override;

  bool ReallySendControlFrame(const QuicFrame& frame) {
    return QuicConnection::SendControlFrame(frame);
  }

  bool ReallySendConnectivityProbingPacket(
      QuicPacketWriter* probing_writer,
      const QuicSocketAddress& peer_address) {
    return QuicConnection::SendConnectivityProbingPacket(probing_writer,
                                                         peer_address);
  }

  void ReallySendConnectivityProbingResponsePacket(
      const QuicSocketAddress& peer_address) {
    QuicConnection::SendConnectivityProbingResponsePacket(peer_address);
  }
  MOCK_METHOD(bool,
              OnPathResponseFrame,
              (const QuicPathResponseFrame&),
              (override));
  MOCK_METHOD(bool,
              OnStopSendingFrame,
              (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(size_t,
              SendCryptoData,
              (EncryptionLevel, size_t, QuicStreamOffset),
              (override));
  size_t QuicConnection_SendCryptoData(EncryptionLevel level,
                                       size_t write_length,
                                       QuicStreamOffset offset) {
    return QuicConnection::SendCryptoData(level, write_length, offset);
  }
};

class PacketSavingConnection : public MockQuicConnection {
 public:
  PacketSavingConnection(MockQuicConnectionHelper* helper,
                         MockAlarmFactory* alarm_factory,
                         Perspective perspective);

  PacketSavingConnection(MockQuicConnectionHelper* helper,
                         MockAlarmFactory* alarm_factory,
                         Perspective perspective,
                         const ParsedQuicVersionVector& supported_versions);
  PacketSavingConnection(const PacketSavingConnection&) = delete;
  PacketSavingConnection& operator=(const PacketSavingConnection&) = delete;

  ~PacketSavingConnection() override;

  void SendOrQueuePacket(SerializedPacket packet) override;

  MOCK_METHOD(void, OnPacketSent, (EncryptionLevel, TransmissionType));

  std::vector<std::unique_ptr<QuicEncryptedPacket>> encrypted_packets_;
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

  MOCK_METHOD(void,
              OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(QuicStream*, CreateIncomingStream, (QuicStreamId id), (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (PendingStream*),
              (override));
  MOCK_METHOD(QuicConsumedData,
              WritevData,
              (QuicStreamId id,
               size_t write_length,
               QuicStreamOffset offset,
               StreamSendingState state,
               TransmissionType type,
               quiche::QuicheOptional<EncryptionLevel> level),
              (override));

  MOCK_METHOD(void,
              SendRstStream,
              (QuicStreamId stream_id,
               QuicRstStreamErrorCode error,
               QuicStreamOffset bytes_written),
              (override));

  MOCK_METHOD(bool, ShouldKeepConnectionAlive, (), (const, override));
  MOCK_METHOD(void,
              SendStopSending,
              (uint16_t code, QuicStreamId stream_id),
              (override));
  MOCK_METHOD(std::vector<std::string>, GetAlpnsToOffer, (), (const, override));
  MOCK_METHOD(std::vector<quiche::QuicheStringPiece>::const_iterator,
              SelectAlpn,
              (const std::vector<quiche::QuicheStringPiece>&),
              (const, override));
  MOCK_METHOD(void, OnAlpnSelected, (quiche::QuicheStringPiece), (override));

  using QuicSession::ActivateStream;

  // Returns a QuicConsumedData that indicates all of |write_length| (and |fin|
  // if set) has been consumed.
  QuicConsumedData ConsumeData(QuicStreamId id,
                               size_t write_length,
                               QuicStreamOffset offset,
                               StreamSendingState state,
                               TransmissionType type,
                               quiche::QuicheOptional<EncryptionLevel> level);

  void ReallySendRstStream(QuicStreamId id,
                           QuicRstStreamErrorCode error,
                           QuicStreamOffset bytes_written) {
    QuicSession::SendRstStream(id, error, bytes_written);
  }

 private:
  std::unique_ptr<QuicCryptoStream> crypto_stream_;
};

class MockQuicCryptoStream : public QuicCryptoStream {
 public:
  explicit MockQuicCryptoStream(QuicSession* session);

  ~MockQuicCryptoStream() override;

  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  void OnPacketDecrypted(EncryptionLevel /*level*/) override {}
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakePacketSent() override {}
  void OnHandshakeDoneReceived() override {}
  HandshakeState GetHandshakeState() const override { return HANDSHAKE_START; }
  void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> /*application_state*/) override {}

 private:
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
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
  MOCK_METHOD(void,
              OnConnectionClosed,
              (const QuicConnectionCloseFrame& frame,
               ConnectionCloseSource source),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (PendingStream*),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateOutgoingBidirectionalStream,
              (),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateOutgoingUnidirectionalStream,
              (),
              (override));
  MOCK_METHOD(bool, ShouldCreateIncomingStream, (QuicStreamId id), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(QuicConsumedData,
              WritevData,
              (QuicStreamId id,
               size_t write_length,
               QuicStreamOffset offset,
               StreamSendingState state,
               TransmissionType type,
               quiche::QuicheOptional<EncryptionLevel> level),
              (override));
  MOCK_METHOD(void,
              SendRstStream,
              (QuicStreamId stream_id,
               QuicRstStreamErrorCode error,
               QuicStreamOffset bytes_written),
              (override));
  MOCK_METHOD(void,
              SendWindowUpdate,
              (QuicStreamId id, QuicStreamOffset byte_offset),
              (override));
  MOCK_METHOD(void, SendBlocked, (QuicStreamId id), (override));
  MOCK_METHOD(void,
              OnStreamHeadersPriority,
              (QuicStreamId stream_id,
               const spdy::SpdyStreamPrecedence& precedence),
              (override));
  MOCK_METHOD(void,
              OnStreamHeaderList,
              (QuicStreamId stream_id,
               bool fin,
               size_t frame_len,
               const QuicHeaderList& header_list),
              (override));
  MOCK_METHOD(void,
              OnPromiseHeaderList,
              (QuicStreamId stream_id,
               QuicStreamId promised_stream_id,
               size_t frame_len,
               const QuicHeaderList& header_list),
              (override));
  MOCK_METHOD(void,
              OnPriorityFrame,
              (QuicStreamId id, const spdy::SpdyStreamPrecedence& precedence),
              (override));

  // Returns a QuicConsumedData that indicates all of |write_length| (and |fin|
  // if set) has been consumed.
  QuicConsumedData ConsumeData(QuicStreamId id,
                               size_t write_length,
                               QuicStreamOffset offset,
                               StreamSendingState state,
                               TransmissionType type,
                               quiche::QuicheOptional<EncryptionLevel> level);

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
  MOCK_METHOD(void,
              OnPeerQpackEncoderStreamCreated,
              (QuicStreamId),
              (override));
  MOCK_METHOD(void,
              OnPeerQpackDecoderStreamCreated,
              (QuicStreamId),
              (override));

  MOCK_METHOD(void,
              OnCancelPushFrameReceived,
              (const CancelPushFrame&),
              (override));
  MOCK_METHOD(void,
              OnSettingsFrameReceived,
              (const SettingsFrame&),
              (override));
  MOCK_METHOD(void, OnGoAwayFrameReceived, (const GoAwayFrame&), (override));
  MOCK_METHOD(void,
              OnMaxPushIdFrameReceived,
              (const MaxPushIdFrame&),
              (override));
  MOCK_METHOD(void,
              OnPriorityUpdateFrameReceived,
              (const PriorityUpdateFrame&),
              (override));

  MOCK_METHOD(void,
              OnDataFrameReceived,
              (QuicStreamId, QuicByteCount),
              (override));
  MOCK_METHOD(void,
              OnHeadersFrameReceived,
              (QuicStreamId, QuicByteCount),
              (override));
  MOCK_METHOD(void,
              OnHeadersDecoded,
              (QuicStreamId, QuicHeaderList),
              (override));
  MOCK_METHOD(void,
              OnPushPromiseFrameReceived,
              (QuicStreamId, QuicStreamId, QuicByteCount),
              (override));
  MOCK_METHOD(void,
              OnPushPromiseDecoded,
              (QuicStreamId, QuicStreamId, QuicHeaderList),
              (override));
  MOCK_METHOD(void,
              OnUnknownFrameReceived,
              (QuicStreamId, uint64_t, QuicByteCount),
              (override));

  MOCK_METHOD(void, OnSettingsFrameSent, (const SettingsFrame&), (override));
  MOCK_METHOD(void, OnGoAwayFrameSent, (QuicStreamId), (override));
  MOCK_METHOD(void, OnMaxPushIdFrameSent, (const MaxPushIdFrame&), (override));
  MOCK_METHOD(void,
              OnPriorityUpdateFrameSent,
              (const PriorityUpdateFrame&),
              (override));

  MOCK_METHOD(void, OnDataFrameSent, (QuicStreamId, QuicByteCount), (override));
  MOCK_METHOD(void,
              OnHeadersFrameSent,
              (QuicStreamId, const spdy::SpdyHeaderBlock&),
              (override));
  MOCK_METHOD(void,
              OnPushPromiseFrameSent,
              (QuicStreamId, QuicStreamId, const spdy::SpdyHeaderBlock&),
              (override));
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

  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (PendingStream*),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateOutgoingBidirectionalStream,
              (),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateOutgoingUnidirectionalStream,
              (),
              (override));
  MOCK_METHOD(std::vector<quiche::QuicheStringPiece>::const_iterator,
              SelectAlpn,
              (const std::vector<quiche::QuicheStringPiece>&),
              (const, override));
  MOCK_METHOD(void, OnAlpnSelected, (quiche::QuicheStringPiece), (override));
  std::unique_ptr<QuicCryptoServerStreamBase> CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override;

  QuicCryptoServerStreamBase* GetMutableCryptoStream() override;

  const QuicCryptoServerStreamBase* GetCryptoStream() const override;

  MockQuicCryptoServerStreamHelper* helper() { return &helper_; }

 private:
  MockQuicSessionVisitor visitor_;
  MockQuicCryptoServerStreamHelper helper_;
};

// A test implementation of QuicClientPushPromiseIndex::Delegate.
class TestPushPromiseDelegate : public QuicClientPushPromiseIndex::Delegate {
 public:
  // |match| sets the validation result for checking whether designated header
  // fields match for promise request and client request.
  explicit TestPushPromiseDelegate(bool match);

  bool CheckVary(const spdy::SpdyHeaderBlock& client_request,
                 const spdy::SpdyHeaderBlock& promise_request,
                 const spdy::SpdyHeaderBlock& promise_response) override;

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
  MOCK_METHOD(void,
              OnProofValid,
              (const QuicCryptoClientConfig::CachedState& cached),
              (override));
  MOCK_METHOD(void,
              OnProofVerifyDetailsAvailable,
              (const ProofVerifyDetails& verify_details),
              (override));

  // TestQuicSpdyClientSession
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (QuicStreamId id),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateIncomingStream,
              (PendingStream*),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateOutgoingBidirectionalStream,
              (),
              (override));
  MOCK_METHOD(QuicSpdyStream*,
              CreateOutgoingUnidirectionalStream,
              (),
              (override));
  MOCK_METHOD(bool, ShouldCreateIncomingStream, (QuicStreamId id), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(bool, ShouldCreateOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(std::vector<std::string>, GetAlpnsToOffer, (), (const, override));
  MOCK_METHOD(void, OnAlpnSelected, (quiche::QuicheStringPiece), (override));

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

  MOCK_METHOD(WriteResult,
              WritePacket,
              (const char*,
               size_t buf_len,
               const QuicIpAddress& self_address,
               const QuicSocketAddress& peer_address,
               PerPacketOptions*),
              (override));
  MOCK_METHOD(bool, IsWriteBlocked, (), (const, override));
  MOCK_METHOD(void, SetWritable, (), (override));
  MOCK_METHOD(QuicByteCount,
              GetMaxPacketSize,
              (const QuicSocketAddress& peer_address),
              (const, override));
  MOCK_METHOD(bool, SupportsReleaseTime, (), (const, override));
  MOCK_METHOD(bool, IsBatchMode, (), (const, override));
  MOCK_METHOD(QuicPacketBuffer,
              GetNextWriteLocation,
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

  MOCK_METHOD(void,
              SetFromConfig,
              (const QuicConfig& config, Perspective perspective),
              (override));
  MOCK_METHOD(void,
              ApplyConnectionOptions,
              (const QuicTagVector& connection_options),
              (override));
  MOCK_METHOD(void,
              SetInitialCongestionWindowInPackets,
              (QuicPacketCount packets),
              (override));
  MOCK_METHOD(void,
              OnCongestionEvent,
              (bool rtt_updated,
               QuicByteCount bytes_in_flight,
               QuicTime event_time,
               const AckedPacketVector& acked_packets,
               const LostPacketVector& lost_packets),
              (override));
  MOCK_METHOD(void,
              OnPacketSent,
              (QuicTime,
               QuicByteCount,
               QuicPacketNumber,
               QuicByteCount,
               HasRetransmittableData),
              (override));
  MOCK_METHOD(void, OnPacketNeutered, (QuicPacketNumber), (override));
  MOCK_METHOD(void, OnRetransmissionTimeout, (bool), (override));
  MOCK_METHOD(void, OnConnectionMigration, (), (override));
  MOCK_METHOD(bool, CanSend, (QuicByteCount), (override));
  MOCK_METHOD(QuicBandwidth, PacingRate, (QuicByteCount), (const, override));
  MOCK_METHOD(QuicBandwidth, BandwidthEstimate, (), (const, override));
  MOCK_METHOD(QuicByteCount, GetCongestionWindow, (), (const, override));
  MOCK_METHOD(std::string, GetDebugState, (), (const, override));
  MOCK_METHOD(bool, InSlowStart, (), (const, override));
  MOCK_METHOD(bool, InRecovery, (), (const, override));
  MOCK_METHOD(bool, ShouldSendProbingPacket, (), (const, override));
  MOCK_METHOD(QuicByteCount, GetSlowStartThreshold, (), (const, override));
  MOCK_METHOD(CongestionControlType,
              GetCongestionControlType,
              (),
              (const, override));
  MOCK_METHOD(void,
              AdjustNetworkParameters,
              (const NetworkParams&),
              (override));
  MOCK_METHOD(void, OnApplicationLimited, (QuicByteCount), (override));
  MOCK_METHOD(void,
              PopulateConnectionStats,
              (QuicConnectionStats*),
              (const, override));
};

class MockLossAlgorithm : public LossDetectionInterface {
 public:
  MockLossAlgorithm();
  MockLossAlgorithm(const MockLossAlgorithm&) = delete;
  MockLossAlgorithm& operator=(const MockLossAlgorithm&) = delete;
  ~MockLossAlgorithm() override;

  MOCK_METHOD(void,
              SetFromConfig,
              (const QuicConfig& config, Perspective perspective),
              (override));

  MOCK_METHOD(DetectionStats,
              DetectLosses,
              (const QuicUnackedPacketMap& unacked_packets,
               QuicTime time,
               const RttStats& rtt_stats,
               QuicPacketNumber largest_recently_acked,
               const AckedPacketVector& packets_acked,
               LostPacketVector*),
              (override));
  MOCK_METHOD(QuicTime, GetLossTimeout, (), (const, override));
  MOCK_METHOD(void,
              SpuriousLossDetected,
              (const QuicUnackedPacketMap&,
               const RttStats&,
               QuicTime,
               QuicPacketNumber,
               QuicPacketNumber),
              (override));

  MOCK_METHOD(void, OnConfigNegotiated, (), (override));
  MOCK_METHOD(void, OnMinRttAvailable, (), (override));
  MOCK_METHOD(void, OnUserAgentIdKnown, (), (override));
  MOCK_METHOD(void, OnConnectionClosed, (), (override));
};

class MockAckListener : public QuicAckListenerInterface {
 public:
  MockAckListener();
  MockAckListener(const MockAckListener&) = delete;
  MockAckListener& operator=(const MockAckListener&) = delete;

  MOCK_METHOD(void,
              OnPacketAcked,
              (int acked_bytes, QuicTime::Delta ack_delay_time),
              (override));

  MOCK_METHOD(void,
              OnPacketRetransmitted,
              (int retransmitted_bytes),
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

  MOCK_METHOD(void,
              OnPacketSent,
              (const SerializedPacket&, TransmissionType, QuicTime),
              (override));

  MOCK_METHOD(void,
              OnCoalescedPacketSent,
              (const QuicCoalescedPacket&, size_t),
              (override));

  MOCK_METHOD(void, OnPingSent, (), (override));

  MOCK_METHOD(void,
              OnPacketReceived,
              (const QuicSocketAddress&,
               const QuicSocketAddress&,
               const QuicEncryptedPacket&),
              (override));

  MOCK_METHOD(void, OnIncorrectConnectionId, (QuicConnectionId), (override));

  MOCK_METHOD(void, OnProtocolVersionMismatch, (ParsedQuicVersion), (override));

  MOCK_METHOD(void,
              OnPacketHeader,
              (const QuicPacketHeader& header),
              (override));

  MOCK_METHOD(void,
              OnSuccessfulVersionNegotiation,
              (const ParsedQuicVersion&),
              (override));

  MOCK_METHOD(void, OnStreamFrame, (const QuicStreamFrame&), (override));

  MOCK_METHOD(void, OnCryptoFrame, (const QuicCryptoFrame&), (override));

  MOCK_METHOD(void,
              OnStopWaitingFrame,
              (const QuicStopWaitingFrame&),
              (override));

  MOCK_METHOD(void, OnRstStreamFrame, (const QuicRstStreamFrame&), (override));

  MOCK_METHOD(void,
              OnConnectionCloseFrame,
              (const QuicConnectionCloseFrame&),
              (override));

  MOCK_METHOD(void, OnBlockedFrame, (const QuicBlockedFrame&), (override));

  MOCK_METHOD(void,
              OnNewConnectionIdFrame,
              (const QuicNewConnectionIdFrame&),
              (override));

  MOCK_METHOD(void,
              OnRetireConnectionIdFrame,
              (const QuicRetireConnectionIdFrame&),
              (override));

  MOCK_METHOD(void, OnNewTokenFrame, (const QuicNewTokenFrame&), (override));

  MOCK_METHOD(void, OnMessageFrame, (const QuicMessageFrame&), (override));

  MOCK_METHOD(void,
              OnStopSendingFrame,
              (const QuicStopSendingFrame&),
              (override));

  MOCK_METHOD(void,
              OnPathChallengeFrame,
              (const QuicPathChallengeFrame&),
              (override));

  MOCK_METHOD(void,
              OnPathResponseFrame,
              (const QuicPathResponseFrame&),
              (override));

  MOCK_METHOD(void,
              OnPublicResetPacket,
              (const QuicPublicResetPacket&),
              (override));

  MOCK_METHOD(void,
              OnVersionNegotiationPacket,
              (const QuicVersionNegotiationPacket&),
              (override));

  MOCK_METHOD(void,
              OnTransportParametersSent,
              (const TransportParameters&),
              (override));

  MOCK_METHOD(void,
              OnTransportParametersReceived,
              (const TransportParameters&),
              (override));
};

class MockReceivedPacketManager : public QuicReceivedPacketManager {
 public:
  explicit MockReceivedPacketManager(QuicConnectionStats* stats);
  ~MockReceivedPacketManager() override;

  MOCK_METHOD(void,
              RecordPacketReceived,
              (const QuicPacketHeader& header, QuicTime receipt_time),
              (override));
  MOCK_METHOD(bool, IsMissing, (QuicPacketNumber packet_number), (override));
  MOCK_METHOD(bool,
              IsAwaitingPacket,
              (QuicPacketNumber packet_number),
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
  MOCK_METHOD(void,
              OnUnrecoverableError,
              (QuicErrorCode, const std::string&),
              (override));
  MOCK_METHOD(bool,
              ShouldGeneratePacket,
              (HasRetransmittableData retransmittable, IsHandshake handshake),
              (override));
  MOCK_METHOD(const QuicFrames,
              MaybeBundleAckOpportunistically,
              (),
              (override));
};

class MockSessionNotifier : public SessionNotifierInterface {
 public:
  MockSessionNotifier();
  ~MockSessionNotifier() override;

  MOCK_METHOD(bool,
              OnFrameAcked,
              (const QuicFrame&, QuicTime::Delta, QuicTime),
              (override));
  MOCK_METHOD(void,
              OnStreamFrameRetransmitted,
              (const QuicStreamFrame&),
              (override));
  MOCK_METHOD(void, OnFrameLost, (const QuicFrame&), (override));
  MOCK_METHOD(void,
              RetransmitFrames,
              (const QuicFrames&, TransmissionType type),
              (override));
  MOCK_METHOD(bool, IsFrameOutstanding, (const QuicFrame&), (const, override));
  MOCK_METHOD(bool, HasUnackedCryptoData, (), (const, override));
  MOCK_METHOD(bool, HasUnackedStreamData, (), (const, override));
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
    QuicServerId server_id,
    QuicTime::Delta connection_start_time,
    const ParsedQuicVersionVector& supported_versions,
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
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
// crypto_server_config: Pointer to the crypto server config.
// server_connection: Pointer reference for newly created
//   connection.  This object will be owned by the
//   server_session.
// server_session: Pointer reference for the newly created server
//   session.  The new object will be owned by the caller.
void CreateServerSessionForTest(
    QuicServerId server_id,
    QuicTime::Delta connection_start_time,
    ParsedQuicVersionVector supported_versions,
    MockQuicConnectionHelper* helper,
    MockAlarmFactory* alarm_factory,
    QuicCryptoServerConfig* crypto_server_config,
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

// Utility function that stores |str|'s data in |iov|.
inline void MakeIOVector(quiche::QuicheStringPiece str, struct iovec* iov) {
  iov->iov_base = const_cast<char*>(str.data());
  iov->iov_len = static_cast<size_t>(str.size());
}

// Helper functions for stream ids, to allow test logic to abstract over the
// HTTP stream numbering scheme (i.e. whether one or two QUIC streams are used
// per HTTP transaction).
QuicStreamId GetNthClientInitiatedBidirectionalStreamId(
    QuicTransportVersion version,
    int n);
QuicStreamId GetNthServerInitiatedBidirectionalStreamId(
    QuicTransportVersion version,
    int n);
QuicStreamId GetNthServerInitiatedUnidirectionalStreamId(
    QuicTransportVersion version,
    int n);
QuicStreamId GetNthClientInitiatedUnidirectionalStreamId(
    QuicTransportVersion version,
    int n);

StreamType DetermineStreamType(QuicStreamId id,
                               ParsedQuicVersion version,
                               Perspective perspective,
                               bool is_incoming,
                               StreamType default_type);

// Utility function that stores message_data in |storage| and returns a
// QuicMemSliceSpan.
QuicMemSliceSpan MakeSpan(QuicBufferAllocator* allocator,
                          quiche::QuicheStringPiece message_data,
                          QuicMemSliceStorage* storage);

// Creates a MemSlice using a singleton trivial buffer allocator.  Performs a
// copy.
QuicMemSlice MemSliceFromString(quiche::QuicheStringPiece data);

// Used to compare ReceivedPacketInfo.
MATCHER_P(ReceivedPacketInfoEquals, info, "") {
  return info.ToString() == arg.ToString();
}

MATCHER_P(ReceivedPacketInfoConnectionIdEquals, destination_connection_id, "") {
  return arg.destination_connection_id == destination_connection_id;
}

MATCHER_P2(InRange, min, max, "") {
  return arg >= min && arg <= max;
}

// A GMock matcher that prints expected and actual QuicErrorCode strings
// upon failure.  Example usage:
// EXPECT_THAT(stream_->connection_error(), IsError(QUIC_INTERNAL_ERROR));
MATCHER_P(IsError,
          expected,
          quiche::QuicheStrCat(negation ? "isn't equal to " : "is equal to ",
                               QuicErrorCodeToString(expected))) {
  *result_listener << QuicErrorCodeToString(static_cast<QuicErrorCode>(arg));
  return arg == expected;
}

// Shorthand for IsError(QUIC_NO_ERROR).
// Example usage: EXPECT_THAT(stream_->connection_error(), IsQuicNoError());
MATCHER(IsQuicNoError,
        quiche::QuicheStrCat(negation ? "isn't equal to " : "is equal to ",
                             QuicErrorCodeToString(QUIC_NO_ERROR))) {
  *result_listener << QuicErrorCodeToString(arg);
  return arg == QUIC_NO_ERROR;
}

// A GMock matcher that prints expected and actual QuicRstStreamErrorCode
// strings upon failure.  Example usage:
// EXPECT_THAT(stream_->stream_error(), IsStreamError(QUIC_INTERNAL_ERROR));
MATCHER_P(IsStreamError,
          expected,
          quiche::QuicheStrCat(negation ? "isn't equal to " : "is equal to ",
                               QuicRstStreamErrorCodeToString(expected))) {
  *result_listener << QuicRstStreamErrorCodeToString(arg);
  return arg == expected;
}

// Shorthand for IsStreamError(QUIC_STREAM_NO_ERROR).  Example usage:
// EXPECT_THAT(stream_->stream_error(), IsQuicStreamNoError());
MATCHER(IsQuicStreamNoError,
        quiche::QuicheStrCat(
            negation ? "isn't equal to " : "is equal to ",
            QuicRstStreamErrorCodeToString(QUIC_STREAM_NO_ERROR))) {
  *result_listener << QuicRstStreamErrorCodeToString(arg);
  return arg == QUIC_STREAM_NO_ERROR;
}

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_UTILS_H_
