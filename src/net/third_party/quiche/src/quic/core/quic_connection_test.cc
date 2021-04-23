// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_connection.h"

#include <errno.h>

#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quic/core/congestion_control/loss_detection_interface.h"
#include "quic/core/congestion_control/send_algorithm_interface.h"
#include "quic/core/crypto/null_decrypter.h"
#include "quic/core/crypto/null_encrypter.h"
#include "quic/core/crypto/quic_decrypter.h"
#include "quic/core/crypto/quic_encrypter.h"
#include "quic/core/frames/quic_connection_close_frame.h"
#include "quic/core/frames/quic_path_response_frame.h"
#include "quic/core/quic_connection_id.h"
#include "quic/core/quic_constants.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_packets.h"
#include "quic/core/quic_path_validator.h"
#include "quic/core/quic_simple_buffer_allocator.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_expect_bug.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_reference_counted.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/mock_clock.h"
#include "quic/test_tools/mock_random.h"
#include "quic/test_tools/quic_config_peer.h"
#include "quic/test_tools/quic_connection_peer.h"
#include "quic/test_tools/quic_framer_peer.h"
#include "quic/test_tools/quic_packet_creator_peer.h"
#include "quic/test_tools/quic_path_validator_peer.h"
#include "quic/test_tools/quic_sent_packet_manager_peer.h"
#include "quic/test_tools/quic_test_utils.h"
#include "quic/test_tools/simple_data_producer.h"
#include "quic/test_tools/simple_session_notifier.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::Ge;
using testing::IgnoreResult;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Lt;
using testing::Ref;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

const char data1[] = "foo data";
const char data2[] = "bar data";

const bool kHasStopWaiting = true;

const int kDefaultRetransmissionTimeMs = 500;

DiversificationNonce kTestDiversificationNonce = {
    'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a',
    'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b',
    'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b',
};

const QuicSocketAddress kPeerAddress =
    QuicSocketAddress(QuicIpAddress::Loopback6(),
                      /*port=*/12345);
const QuicSocketAddress kSelfAddress =
    QuicSocketAddress(QuicIpAddress::Loopback6(),
                      /*port=*/443);

QuicStreamId GetNthClientInitiatedStreamId(int n,
                                           QuicTransportVersion version) {
  return QuicUtils::GetFirstBidirectionalStreamId(version,
                                                  Perspective::IS_CLIENT) +
         n * 2;
}

QuicLongHeaderType EncryptionlevelToLongHeaderType(EncryptionLevel level) {
  switch (level) {
    case ENCRYPTION_INITIAL:
      return INITIAL;
    case ENCRYPTION_HANDSHAKE:
      return HANDSHAKE;
    case ENCRYPTION_ZERO_RTT:
      return ZERO_RTT_PROTECTED;
    case ENCRYPTION_FORWARD_SECURE:
      QUICHE_DCHECK(false);
      return INVALID_PACKET_TYPE;
    default:
      QUICHE_DCHECK(false);
      return INVALID_PACKET_TYPE;
  }
}

// A NullEncrypterWithConfidentialityLimit is a NullEncrypter that allows
// specifying the confidentiality limit on the maximum number of packets that
// may be encrypted per key phase in TLS+QUIC.
class NullEncrypterWithConfidentialityLimit : public NullEncrypter {
 public:
  NullEncrypterWithConfidentialityLimit(Perspective perspective,
                                        QuicPacketCount confidentiality_limit)
      : NullEncrypter(perspective),
        confidentiality_limit_(confidentiality_limit) {}

  QuicPacketCount GetConfidentialityLimit() const override {
    return confidentiality_limit_;
  }

 private:
  QuicPacketCount confidentiality_limit_;
};

class StrictTaggingDecrypterWithIntegrityLimit : public StrictTaggingDecrypter {
 public:
  StrictTaggingDecrypterWithIntegrityLimit(uint8_t tag,
                                           QuicPacketCount integrity_limit)
      : StrictTaggingDecrypter(tag), integrity_limit_(integrity_limit) {}

  QuicPacketCount GetIntegrityLimit() const override {
    return integrity_limit_;
  }

 private:
  QuicPacketCount integrity_limit_;
};

class TestConnectionHelper : public QuicConnectionHelperInterface {
 public:
  TestConnectionHelper(MockClock* clock, MockRandom* random_generator)
      : clock_(clock), random_generator_(random_generator) {
    clock_->AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }
  TestConnectionHelper(const TestConnectionHelper&) = delete;
  TestConnectionHelper& operator=(const TestConnectionHelper&) = delete;

  // QuicConnectionHelperInterface
  const QuicClock* GetClock() const override { return clock_; }

  QuicRandom* GetRandomGenerator() override { return random_generator_; }

  QuicBufferAllocator* GetStreamSendBufferAllocator() override {
    return &buffer_allocator_;
  }

 private:
  MockClock* clock_;
  MockRandom* random_generator_;
  SimpleBufferAllocator buffer_allocator_;
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

class TestConnection : public QuicConnection {
 public:
  TestConnection(QuicConnectionId connection_id,
                 QuicSocketAddress initial_self_address,
                 QuicSocketAddress initial_peer_address,
                 TestConnectionHelper* helper,
                 TestAlarmFactory* alarm_factory,
                 TestPacketWriter* writer,
                 Perspective perspective,
                 ParsedQuicVersion version)
      : QuicConnection(connection_id,
                       initial_self_address,
                       initial_peer_address,
                       helper,
                       alarm_factory,
                       writer,
                       /* owns_writer= */ false,
                       perspective,
                       SupportedVersions(version)),
        notifier_(nullptr) {
    writer->set_perspective(perspective);
    SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                 std::make_unique<NullEncrypter>(perspective));
    SetDataProducer(&producer_);
  }
  TestConnection(const TestConnection&) = delete;
  TestConnection& operator=(const TestConnection&) = delete;

  void SetSendAlgorithm(SendAlgorithmInterface* send_algorithm) {
    QuicConnectionPeer::SetSendAlgorithm(this, send_algorithm);
  }

  void SetLossAlgorithm(LossDetectionInterface* loss_algorithm) {
    QuicConnectionPeer::SetLossAlgorithm(this, loss_algorithm);
  }

  void SendPacket(EncryptionLevel /*level*/,
                  uint64_t packet_number,
                  std::unique_ptr<QuicPacket> packet,
                  HasRetransmittableData retransmittable,
                  bool has_ack,
                  bool has_pending_frames) {
    ScopedPacketFlusher flusher(this);
    char buffer[kMaxOutgoingPacketSize];
    size_t encrypted_length =
        QuicConnectionPeer::GetFramer(this)->EncryptPayload(
            ENCRYPTION_INITIAL, QuicPacketNumber(packet_number), *packet,
            buffer, kMaxOutgoingPacketSize);
    SerializedPacket serialized_packet(
        QuicPacketNumber(packet_number), PACKET_4BYTE_PACKET_NUMBER, buffer,
        encrypted_length, has_ack, has_pending_frames);
    serialized_packet.peer_address = kPeerAddress;
    if (retransmittable == HAS_RETRANSMITTABLE_DATA) {
      serialized_packet.retransmittable_frames.push_back(
          QuicFrame(QuicPingFrame()));
    }
    OnSerializedPacket(std::move(serialized_packet));
  }

  QuicConsumedData SaveAndSendStreamData(QuicStreamId id,
                                         const struct iovec* iov,
                                         int iov_count,
                                         size_t total_length,
                                         QuicStreamOffset offset,
                                         StreamSendingState state) {
    ScopedPacketFlusher flusher(this);
    producer_.SaveStreamData(id, iov, iov_count, 0u, total_length);
    if (notifier_ != nullptr) {
      return notifier_->WriteOrBufferData(id, total_length, state);
    }
    return QuicConnection::SendStreamData(id, total_length, offset, state);
  }

  QuicConsumedData SendStreamDataWithString(QuicStreamId id,
                                            absl::string_view data,
                                            QuicStreamOffset offset,
                                            StreamSendingState state) {
    ScopedPacketFlusher flusher(this);
    if (!QuicUtils::IsCryptoStreamId(transport_version(), id) &&
        this->encryption_level() == ENCRYPTION_INITIAL) {
      this->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
      if (perspective() == Perspective::IS_CLIENT && !IsHandshakeComplete()) {
        OnHandshakeComplete();
      }
      if (version().SupportsAntiAmplificationLimit()) {
        QuicConnectionPeer::SetAddressValidated(this);
      }
    }
    struct iovec iov;
    MakeIOVector(data, &iov);
    return SaveAndSendStreamData(id, &iov, 1, data.length(), offset, state);
  }

  QuicConsumedData SendApplicationDataAtLevel(EncryptionLevel encryption_level,
                                              QuicStreamId id,
                                              absl::string_view data,
                                              QuicStreamOffset offset,
                                              StreamSendingState state) {
    ScopedPacketFlusher flusher(this);
    QUICHE_DCHECK(encryption_level >= ENCRYPTION_ZERO_RTT);
    SetEncrypter(encryption_level, std::make_unique<TaggingEncrypter>(0x01));
    SetDefaultEncryptionLevel(encryption_level);
    struct iovec iov;
    MakeIOVector(data, &iov);
    return SaveAndSendStreamData(id, &iov, 1, data.length(), offset, state);
  }

  QuicConsumedData SendStreamData3() {
    return SendStreamDataWithString(
        GetNthClientInitiatedStreamId(1, transport_version()), "food", 0,
        NO_FIN);
  }

  QuicConsumedData SendStreamData5() {
    return SendStreamDataWithString(
        GetNthClientInitiatedStreamId(2, transport_version()), "food2", 0,
        NO_FIN);
  }

  // Ensures the connection can write stream data before writing.
  QuicConsumedData EnsureWritableAndSendStreamData5() {
    EXPECT_TRUE(CanWrite(HAS_RETRANSMITTABLE_DATA));
    return SendStreamData5();
  }

  // The crypto stream has special semantics so that it is not blocked by a
  // congestion window limitation, and also so that it gets put into a separate
  // packet (so that it is easier to reason about a crypto frame not being
  // split needlessly across packet boundaries).  As a result, we have separate
  // tests for some cases for this stream.
  QuicConsumedData SendCryptoStreamData() {
    QuicStreamOffset offset = 0;
    absl::string_view data("chlo");
    if (!QuicVersionUsesCryptoFrames(transport_version())) {
      return SendCryptoDataWithString(data, offset);
    }
    producer_.SaveCryptoData(ENCRYPTION_INITIAL, offset, data);
    size_t bytes_written;
    if (notifier_) {
      bytes_written =
          notifier_->WriteCryptoData(ENCRYPTION_INITIAL, data.length(), offset);
    } else {
      bytes_written = QuicConnection::SendCryptoData(ENCRYPTION_INITIAL,
                                                     data.length(), offset);
    }
    return QuicConsumedData(bytes_written, /*fin_consumed*/ false);
  }

  QuicConsumedData SendCryptoDataWithString(absl::string_view data,
                                            QuicStreamOffset offset) {
    return SendCryptoDataWithString(data, offset, ENCRYPTION_INITIAL);
  }

  QuicConsumedData SendCryptoDataWithString(absl::string_view data,
                                            QuicStreamOffset offset,
                                            EncryptionLevel encryption_level) {
    if (!QuicVersionUsesCryptoFrames(transport_version())) {
      return SendStreamDataWithString(
          QuicUtils::GetCryptoStreamId(transport_version()), data, offset,
          NO_FIN);
    }
    producer_.SaveCryptoData(encryption_level, offset, data);
    size_t bytes_written;
    if (notifier_) {
      bytes_written =
          notifier_->WriteCryptoData(encryption_level, data.length(), offset);
    } else {
      bytes_written = QuicConnection::SendCryptoData(encryption_level,
                                                     data.length(), offset);
    }
    return QuicConsumedData(bytes_written, /*fin_consumed*/ false);
  }

  void set_version(ParsedQuicVersion version) {
    QuicConnectionPeer::GetFramer(this)->set_version(version);
  }

  void SetSupportedVersions(const ParsedQuicVersionVector& versions) {
    QuicConnectionPeer::GetFramer(this)->SetSupportedVersions(versions);
    writer()->SetSupportedVersions(versions);
  }

  // This should be called before setting customized encrypters/decrypters for
  // connection and peer creator.
  void set_perspective(Perspective perspective) {
    writer()->set_perspective(perspective);
    QuicConnectionPeer::SetPerspective(this, perspective);
    QuicSentPacketManagerPeer::SetPerspective(
        QuicConnectionPeer::GetSentPacketManager(this), perspective);
    QuicConnectionPeer::GetFramer(this)->SetInitialObfuscators(
        TestConnectionId());
    for (EncryptionLevel level : {ENCRYPTION_ZERO_RTT, ENCRYPTION_HANDSHAKE,
                                  ENCRYPTION_FORWARD_SECURE}) {
      if (QuicConnectionPeer::GetFramer(this)->HasEncrypterOfEncryptionLevel(
              level)) {
        SetEncrypter(level, std::make_unique<NullEncrypter>(perspective));
      }
      if (QuicConnectionPeer::GetFramer(this)->HasDecrypterOfEncryptionLevel(
              level)) {
        InstallDecrypter(level, std::make_unique<NullDecrypter>(perspective));
      }
    }
  }

  // Enable path MTU discovery.  Assumes that the test is performed from the
  // server perspective and the higher value of MTU target is used.
  void EnablePathMtuDiscovery(MockSendAlgorithm* send_algorithm) {
    ASSERT_EQ(Perspective::IS_SERVER, perspective());

    if (GetQuicReloadableFlag(quic_enable_mtu_discovery_at_server)) {
      OnConfigNegotiated();
    } else {
      QuicConfig config;
      QuicTagVector connection_options;
      connection_options.push_back(kMTUH);
      config.SetInitialReceivedConnectionOptions(connection_options);
      EXPECT_CALL(*send_algorithm, SetFromConfig(_, _));
      SetFromConfig(config);
    }

    // Normally, the pacing would be disabled in the test, but calling
    // SetFromConfig enables it.  Set nearly-infinite bandwidth to make the
    // pacing algorithm work.
    EXPECT_CALL(*send_algorithm, PacingRate(_))
        .WillRepeatedly(Return(QuicBandwidth::Infinite()));
  }

  TestAlarmFactory::TestAlarm* GetAckAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetAckAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetPingAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetPingAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetRetransmissionAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetRetransmissionAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetSendAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetSendAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetTimeoutAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetIdleNetworkDetectorAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetMtuDiscoveryAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetMtuDiscoveryAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetProcessUndecryptablePacketsAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetProcessUndecryptablePacketsAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetDiscardPreviousOneRttKeysAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetDiscardPreviousOneRttKeysAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetDiscardZeroRttDecryptionKeysAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetDiscardZeroRttDecryptionKeysAlarm(this));
  }

  TestAlarmFactory::TestAlarm* GetBlackholeDetectorAlarm() {
    return reinterpret_cast<TestAlarmFactory::TestAlarm*>(
        QuicConnectionPeer::GetBlackholeDetectorAlarm(this));
  }

  void PathDegradingTimeout() {
    QUICHE_DCHECK(PathDegradingDetectionInProgress());
    GetBlackholeDetectorAlarm()->Fire();
  }

  bool PathDegradingDetectionInProgress() {
    return QuicConnectionPeer::GetPathDegradingDeadline(this).IsInitialized();
  }

  bool BlackholeDetectionInProgress() {
    return QuicConnectionPeer::GetBlackholeDetectionDeadline(this)
        .IsInitialized();
  }

  bool PathMtuReductionDetectionInProgress() {
    return QuicConnectionPeer::GetPathMtuReductionDetectionDeadline(this)
        .IsInitialized();
  }

  void SetMaxTailLossProbes(size_t max_tail_loss_probes) {
    QuicSentPacketManagerPeer::SetMaxTailLossProbes(
        QuicConnectionPeer::GetSentPacketManager(this), max_tail_loss_probes);
  }

  QuicByteCount GetBytesInFlight() {
    return QuicConnectionPeer::GetSentPacketManager(this)->GetBytesInFlight();
  }

  void set_notifier(SimpleSessionNotifier* notifier) { notifier_ = notifier; }

  void ReturnEffectivePeerAddressForNextPacket(const QuicSocketAddress& addr) {
    next_effective_peer_addr_ = std::make_unique<QuicSocketAddress>(addr);
  }

  bool PtoEnabled() {
    if (QuicConnectionPeer::GetSentPacketManager(this)->pto_enabled()) {
      // PTO mode is default enabled for T099. And TLP/RTO related tests are
      // stale.
      QUICHE_DCHECK(PROTOCOL_TLS1_3 == version().handshake_protocol ||
                    GetQuicReloadableFlag(quic_default_on_pto));
      return true;
    }
    return false;
  }

  SimpleDataProducer* producer() { return &producer_; }

  using QuicConnection::active_effective_peer_migration_type;
  using QuicConnection::IsCurrentPacketConnectivityProbing;
  using QuicConnection::SelectMutualVersion;
  using QuicConnection::SendProbingRetransmissions;
  using QuicConnection::set_defer_send_in_response_to_packets;

 protected:
  QuicSocketAddress GetEffectivePeerAddressFromCurrentPacket() const override {
    if (next_effective_peer_addr_) {
      return *std::move(next_effective_peer_addr_);
    }
    return QuicConnection::GetEffectivePeerAddressFromCurrentPacket();
  }

 private:
  TestPacketWriter* writer() {
    return static_cast<TestPacketWriter*>(QuicConnection::writer());
  }

  SimpleDataProducer producer_;

  SimpleSessionNotifier* notifier_;

  std::unique_ptr<QuicSocketAddress> next_effective_peer_addr_;
};

enum class AckResponse { kDefer, kImmediate };

// Run tests with combinations of {ParsedQuicVersion, AckResponse}.
struct TestParams {
  TestParams(ParsedQuicVersion version,
             AckResponse ack_response,
             bool no_stop_waiting)
      : version(version),
        ack_response(ack_response),
        no_stop_waiting(no_stop_waiting) {}

  ParsedQuicVersion version;
  AckResponse ack_response;
  bool no_stop_waiting;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  return absl::StrCat(
      ParsedQuicVersionToString(p.version), "_",
      (p.ack_response == AckResponse::kDefer ? "defer" : "immediate"), "_",
      (p.no_stop_waiting ? "No" : ""), "StopWaiting");
}

// Constructs various test permutations.
std::vector<TestParams> GetTestParams() {
  QuicFlagSaver flags;
  std::vector<TestParams> params;
  ParsedQuicVersionVector all_supported_versions = AllSupportedVersions();
  for (size_t i = 0; i < all_supported_versions.size(); ++i) {
    for (AckResponse ack_response :
         {AckResponse::kDefer, AckResponse::kImmediate}) {
      params.push_back(
          TestParams(all_supported_versions[i], ack_response, true));
      if (!all_supported_versions[i].HasIetfInvariantHeader()) {
        params.push_back(
            TestParams(all_supported_versions[i], ack_response, false));
      }
    }
  }
  return params;
}

class QuicConnectionTest : public QuicTestWithParam<TestParams> {
 public:
  // For tests that do silent connection closes, no such packet is generated. In
  // order to verify the contents of the OnConnectionClosed upcall, EXPECTs
  // should invoke this method, saving the frame, and then the test can verify
  // the contents.
  void SaveConnectionCloseFrame(const QuicConnectionCloseFrame& frame,
                                ConnectionCloseSource /*source*/) {
    saved_connection_close_frame_ = frame;
    connection_close_frame_count_++;
  }

 protected:
  QuicConnectionTest()
      : connection_id_(TestConnectionId()),
        framer_(SupportedVersions(version()),
                QuicTime::Zero(),
                Perspective::IS_CLIENT,
                connection_id_.length()),
        send_algorithm_(new StrictMock<MockSendAlgorithm>),
        loss_algorithm_(new MockLossAlgorithm()),
        helper_(new TestConnectionHelper(&clock_, &random_generator_)),
        alarm_factory_(new TestAlarmFactory()),
        peer_framer_(SupportedVersions(version()),
                     QuicTime::Zero(),
                     Perspective::IS_SERVER,
                     connection_id_.length()),
        peer_creator_(connection_id_,
                      &peer_framer_,
                      /*delegate=*/nullptr),
        writer_(
            new TestPacketWriter(version(), &clock_, Perspective::IS_CLIENT)),
        connection_(connection_id_,
                    kSelfAddress,
                    kPeerAddress,
                    helper_.get(),
                    alarm_factory_.get(),
                    writer_.get(),
                    Perspective::IS_CLIENT,
                    version()),
        creator_(QuicConnectionPeer::GetPacketCreator(&connection_)),
        manager_(QuicConnectionPeer::GetSentPacketManager(&connection_)),
        frame1_(0, false, 0, absl::string_view(data1)),
        frame2_(0, false, 3, absl::string_view(data2)),
        crypto_frame_(ENCRYPTION_INITIAL, 0, absl::string_view(data1)),
        packet_number_length_(PACKET_4BYTE_PACKET_NUMBER),
        connection_id_included_(CONNECTION_ID_PRESENT),
        notifier_(&connection_),
        connection_close_frame_count_(0) {
    QUIC_DVLOG(2) << "QuicConnectionTest(" << PrintToString(GetParam()) << ")";
    connection_.set_defer_send_in_response_to_packets(GetParam().ack_response ==
                                                      AckResponse::kDefer);
    framer_.SetInitialObfuscators(TestConnectionId());
    connection_.InstallInitialCrypters(TestConnectionId());
    CrypterPair crypters;
    CryptoUtils::CreateInitialObfuscators(Perspective::IS_SERVER, version(),
                                          TestConnectionId(), &crypters);
    peer_creator_.SetEncrypter(ENCRYPTION_INITIAL,
                               std::move(crypters.encrypter));
    if (version().KnowsWhichDecrypterToUse()) {
      peer_framer_.InstallDecrypter(ENCRYPTION_INITIAL,
                                    std::move(crypters.decrypter));
    } else {
      peer_framer_.SetDecrypter(ENCRYPTION_INITIAL,
                                std::move(crypters.decrypter));
    }
    for (EncryptionLevel level :
         {ENCRYPTION_ZERO_RTT, ENCRYPTION_FORWARD_SECURE}) {
      peer_creator_.SetEncrypter(
          level, std::make_unique<NullEncrypter>(peer_framer_.perspective()));
    }
    QuicFramerPeer::SetLastSerializedServerConnectionId(
        QuicConnectionPeer::GetFramer(&connection_), connection_id_);
    QuicFramerPeer::SetLastWrittenPacketNumberLength(
        QuicConnectionPeer::GetFramer(&connection_), packet_number_length_);
    if (version().HasIetfInvariantHeader()) {
      EXPECT_TRUE(QuicConnectionPeer::GetNoStopWaitingFrames(&connection_));
    } else {
      QuicConnectionPeer::SetNoStopWaitingFrames(&connection_,
                                                 GetParam().no_stop_waiting);
    }
    QuicStreamId stream_id;
    if (QuicVersionUsesCryptoFrames(version().transport_version)) {
      stream_id = QuicUtils::GetFirstBidirectionalStreamId(
          version().transport_version, Perspective::IS_CLIENT);
    } else {
      stream_id = QuicUtils::GetCryptoStreamId(version().transport_version);
    }
    frame1_.stream_id = stream_id;
    frame2_.stream_id = stream_id;
    connection_.set_visitor(&visitor_);
    connection_.SetSessionNotifier(&notifier_);
    connection_.set_notifier(&notifier_);
    connection_.SetSendAlgorithm(send_algorithm_);
    connection_.SetLossAlgorithm(loss_algorithm_.get());
    EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, OnPacketNeutered(_)).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
        .WillRepeatedly(Return(kDefaultTCPMSS));
    EXPECT_CALL(*send_algorithm_, PacingRate(_))
        .WillRepeatedly(Return(QuicBandwidth::Zero()));
    EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
        .Times(AnyNumber())
        .WillRepeatedly(Return(QuicBandwidth::Zero()));
    EXPECT_CALL(*send_algorithm_, PopulateConnectionStats(_))
        .Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, InSlowStart()).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, InRecovery()).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, GetCongestionControlType())
        .Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, GetCongestionControlType())
        .Times(AnyNumber());
    EXPECT_CALL(visitor_, WillingAndAbleToWrite()).Times(AnyNumber());
    EXPECT_CALL(visitor_, OnPacketDecrypted(_)).Times(AnyNumber());
    EXPECT_CALL(visitor_, OnCanWrite())
        .WillRepeatedly(Invoke(&notifier_, &SimpleSessionNotifier::OnCanWrite));
    EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(visitor_, OnCongestionWindowChange(_)).Times(AnyNumber());
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_)).Times(AnyNumber());
    EXPECT_CALL(visitor_, OnOneRttPacketAcknowledged())
        .Times(testing::AtMost(1));
    EXPECT_CALL(*loss_algorithm_, GetLossTimeout())
        .WillRepeatedly(Return(QuicTime::Zero()));
    EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_START));
    if (connection_.version().KnowsWhichDecrypterToUse()) {
      connection_.InstallDecrypter(
          ENCRYPTION_FORWARD_SECURE,
          std::make_unique<NullDecrypter>(Perspective::IS_CLIENT));
    }
    peer_creator_.SetDefaultPeerAddress(kSelfAddress);
  }

  QuicConnectionTest(const QuicConnectionTest&) = delete;
  QuicConnectionTest& operator=(const QuicConnectionTest&) = delete;

  ParsedQuicVersion version() { return GetParam().version; }

  QuicStopWaitingFrame* stop_waiting() {
    QuicConnectionPeer::PopulateStopWaitingFrame(&connection_, &stop_waiting_);
    return &stop_waiting_;
  }

  QuicPacketNumber least_unacked() {
    if (writer_->stop_waiting_frames().empty()) {
      return QuicPacketNumber();
    }
    return writer_->stop_waiting_frames()[0].least_unacked;
  }

  void use_tagging_decrypter() { writer_->use_tagging_decrypter(); }

  void SetDecrypter(EncryptionLevel level,
                    std::unique_ptr<QuicDecrypter> decrypter) {
    if (connection_.version().KnowsWhichDecrypterToUse()) {
      connection_.InstallDecrypter(level, std::move(decrypter));
      connection_.RemoveDecrypter(ENCRYPTION_INITIAL);
    } else {
      connection_.SetDecrypter(level, std::move(decrypter));
    }
  }

  void ProcessPacket(uint64_t number) {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacket(number);
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
  }

  void ProcessReceivedPacket(const QuicSocketAddress& self_address,
                             const QuicSocketAddress& peer_address,
                             const QuicReceivedPacket& packet) {
    connection_.ProcessUdpPacket(self_address, peer_address, packet);
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
  }

  QuicFrame MakeCryptoFrame() const {
    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      return QuicFrame(new QuicCryptoFrame(crypto_frame_));
    }
    return QuicFrame(QuicStreamFrame(
        QuicUtils::GetCryptoStreamId(connection_.transport_version()), false,
        0u, absl::string_view()));
  }

  void ProcessFramePacket(QuicFrame frame) {
    ProcessFramePacketWithAddresses(frame, kSelfAddress, kPeerAddress,
                                    ENCRYPTION_FORWARD_SECURE);
  }

  void ProcessFramePacketWithAddresses(QuicFrame frame,
                                       QuicSocketAddress self_address,
                                       QuicSocketAddress peer_address,
                                       EncryptionLevel level) {
    QuicFrames frames;
    frames.push_back(QuicFrame(frame));
    return ProcessFramesPacketWithAddresses(frames, self_address, peer_address,
                                            level);
  }

  void ProcessFramesPacketWithAddresses(QuicFrames frames,
                                        QuicSocketAddress self_address,
                                        QuicSocketAddress peer_address,
                                        EncryptionLevel level) {
    QUICHE_DCHECK(peer_framer_.HasEncrypterOfEncryptionLevel(level));
    peer_creator_.set_encryption_level(level);
    QuicPacketCreatorPeer::SetSendVersionInPacket(
        &peer_creator_,
        level < ENCRYPTION_FORWARD_SECURE &&
            connection_.perspective() == Perspective::IS_SERVER);

    char buffer[kMaxOutgoingPacketSize];
    SerializedPacket serialized_packet =
        QuicPacketCreatorPeer::SerializeAllFrames(
            &peer_creator_, frames, buffer, kMaxOutgoingPacketSize);
    connection_.ProcessUdpPacket(
        self_address, peer_address,
        QuicReceivedPacket(serialized_packet.encrypted_buffer,
                           serialized_packet.encrypted_length, clock_.Now()));
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
  }

  // Bypassing the packet creator is unrealistic, but allows us to process
  // packets the QuicPacketCreator won't allow us to create.
  void ForceProcessFramePacket(QuicFrame frame) {
    QuicFrames frames;
    frames.push_back(QuicFrame(frame));
    bool send_version = connection_.perspective() == Perspective::IS_SERVER;
    if (connection_.version().KnowsWhichDecrypterToUse()) {
      send_version = true;
    }
    QuicPacketCreatorPeer::SetSendVersionInPacket(&peer_creator_, send_version);
    QuicPacketHeader header;
    QuicPacketCreatorPeer::FillPacketHeader(&peer_creator_, &header);
    char encrypted_buffer[kMaxOutgoingPacketSize];
    size_t length = peer_framer_.BuildDataPacket(
        header, frames, encrypted_buffer, kMaxOutgoingPacketSize,
        ENCRYPTION_INITIAL);
    QUICHE_DCHECK_GT(length, 0u);

    const size_t encrypted_length = peer_framer_.EncryptInPlace(
        ENCRYPTION_INITIAL, header.packet_number,
        GetStartOfEncryptedData(peer_framer_.version().transport_version,
                                header),
        length, kMaxOutgoingPacketSize, encrypted_buffer);
    QUICHE_DCHECK_GT(encrypted_length, 0u);

    connection_.ProcessUdpPacket(
        kSelfAddress, kPeerAddress,
        QuicReceivedPacket(encrypted_buffer, encrypted_length, clock_.Now()));
  }

  size_t ProcessFramePacketAtLevel(uint64_t number,
                                   QuicFrame frame,
                                   EncryptionLevel level) {
    QuicFrames frames;
    frames.push_back(frame);
    return ProcessFramesPacketAtLevel(number, frames, level);
  }

  size_t ProcessFramesPacketAtLevel(uint64_t number,
                                    const QuicFrames& frames,
                                    EncryptionLevel level) {
    QuicPacketHeader header = ConstructPacketHeader(number, level);
    // Set the correct encryption level and encrypter on peer_creator and
    // peer_framer, respectively.
    peer_creator_.set_encryption_level(level);
    if (QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_) >
        ENCRYPTION_INITIAL) {
      peer_framer_.SetEncrypter(
          QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_),
          std::make_unique<TaggingEncrypter>(0x01));
      // Set the corresponding decrypter.
      if (connection_.version().KnowsWhichDecrypterToUse()) {
        connection_.InstallDecrypter(
            QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_),
            std::make_unique<StrictTaggingDecrypter>(0x01));
        connection_.RemoveDecrypter(ENCRYPTION_INITIAL);
      } else {
        connection_.SetDecrypter(
            QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_),
            std::make_unique<StrictTaggingDecrypter>(0x01));
      }
    }
    std::unique_ptr<QuicPacket> packet(ConstructPacket(header, frames));

    char buffer[kMaxOutgoingPacketSize];
    size_t encrypted_length =
        peer_framer_.EncryptPayload(level, QuicPacketNumber(number), *packet,
                                    buffer, kMaxOutgoingPacketSize);
    connection_.ProcessUdpPacket(
        kSelfAddress, kPeerAddress,
        QuicReceivedPacket(buffer, encrypted_length, clock_.Now(), false));
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
    return encrypted_length;
  }

  struct PacketInfo {
    PacketInfo(uint64_t packet_number, QuicFrames frames, EncryptionLevel level)
        : packet_number(packet_number), frames(frames), level(level) {}

    uint64_t packet_number;
    QuicFrames frames;
    EncryptionLevel level;
  };

  size_t ProcessCoalescedPacket(std::vector<PacketInfo> packets) {
    char coalesced_buffer[kMaxOutgoingPacketSize];
    size_t coalesced_size = 0;
    bool contains_initial = false;
    for (const auto& packet : packets) {
      QuicPacketHeader header =
          ConstructPacketHeader(packet.packet_number, packet.level);
      // Set the correct encryption level and encrypter on peer_creator and
      // peer_framer, respectively.
      peer_creator_.set_encryption_level(packet.level);
      if (packet.level == ENCRYPTION_INITIAL) {
        contains_initial = true;
      }
      if (QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_) >
          ENCRYPTION_INITIAL) {
        peer_framer_.SetEncrypter(
            QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_),
            std::make_unique<TaggingEncrypter>(0x01));
        // Set the corresponding decrypter.
        if (connection_.version().KnowsWhichDecrypterToUse()) {
          connection_.InstallDecrypter(
              QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_),
              std::make_unique<StrictTaggingDecrypter>(0x01));
        } else {
          connection_.SetDecrypter(
              QuicPacketCreatorPeer::GetEncryptionLevel(&peer_creator_),
              std::make_unique<StrictTaggingDecrypter>(0x01));
        }
      }
      std::unique_ptr<QuicPacket> constructed_packet(
          ConstructPacket(header, packet.frames));

      char buffer[kMaxOutgoingPacketSize];
      size_t encrypted_length = peer_framer_.EncryptPayload(
          packet.level, QuicPacketNumber(packet.packet_number),
          *constructed_packet, buffer, kMaxOutgoingPacketSize);
      QUICHE_DCHECK_LE(coalesced_size + encrypted_length,
                       kMaxOutgoingPacketSize);
      memcpy(coalesced_buffer + coalesced_size, buffer, encrypted_length);
      coalesced_size += encrypted_length;
    }
    if (contains_initial) {
      // Padded coalesced packet to full if it contains initial packet.
      memset(coalesced_buffer + coalesced_size, '0',
             kMaxOutgoingPacketSize - coalesced_size);
    }
    connection_.ProcessUdpPacket(
        kSelfAddress, kPeerAddress,
        QuicReceivedPacket(coalesced_buffer, coalesced_size, clock_.Now(),
                           false));
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
    return coalesced_size;
  }

  size_t ProcessDataPacket(uint64_t number) {
    return ProcessDataPacketAtLevel(number, false, ENCRYPTION_FORWARD_SECURE);
  }

  size_t ProcessDataPacket(QuicPacketNumber packet_number) {
    return ProcessDataPacketAtLevel(packet_number, false,
                                    ENCRYPTION_FORWARD_SECURE);
  }

  size_t ProcessDataPacketAtLevel(QuicPacketNumber packet_number,
                                  bool has_stop_waiting,
                                  EncryptionLevel level) {
    return ProcessDataPacketAtLevel(packet_number.ToUint64(), has_stop_waiting,
                                    level);
  }

  size_t ProcessCryptoPacketAtLevel(uint64_t number, EncryptionLevel level) {
    QuicPacketHeader header = ConstructPacketHeader(number, level);
    QuicFrames frames;
    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      frames.push_back(QuicFrame(&crypto_frame_));
    } else {
      frames.push_back(QuicFrame(frame1_));
    }
    if (level == ENCRYPTION_INITIAL) {
      frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
    }
    std::unique_ptr<QuicPacket> packet = ConstructPacket(header, frames);
    char buffer[kMaxOutgoingPacketSize];
    peer_creator_.set_encryption_level(level);
    size_t encrypted_length =
        peer_framer_.EncryptPayload(level, QuicPacketNumber(number), *packet,
                                    buffer, kMaxOutgoingPacketSize);
    connection_.ProcessUdpPacket(
        kSelfAddress, kPeerAddress,
        QuicReceivedPacket(buffer, encrypted_length, clock_.Now(), false));
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
    return encrypted_length;
  }

  size_t ProcessDataPacketAtLevel(uint64_t number,
                                  bool has_stop_waiting,
                                  EncryptionLevel level) {
    std::unique_ptr<QuicPacket> packet(
        ConstructDataPacket(number, has_stop_waiting, level));
    char buffer[kMaxOutgoingPacketSize];
    peer_creator_.set_encryption_level(level);
    size_t encrypted_length =
        peer_framer_.EncryptPayload(level, QuicPacketNumber(number), *packet,
                                    buffer, kMaxOutgoingPacketSize);
    connection_.ProcessUdpPacket(
        kSelfAddress, kPeerAddress,
        QuicReceivedPacket(buffer, encrypted_length, clock_.Now(), false));
    if (connection_.GetSendAlarm()->IsSet()) {
      connection_.GetSendAlarm()->Fire();
    }
    return encrypted_length;
  }

  void ProcessClosePacket(uint64_t number) {
    std::unique_ptr<QuicPacket> packet(ConstructClosePacket(number));
    char buffer[kMaxOutgoingPacketSize];
    size_t encrypted_length = peer_framer_.EncryptPayload(
        ENCRYPTION_FORWARD_SECURE, QuicPacketNumber(number), *packet, buffer,
        kMaxOutgoingPacketSize);
    connection_.ProcessUdpPacket(
        kSelfAddress, kPeerAddress,
        QuicReceivedPacket(buffer, encrypted_length, QuicTime::Zero(), false));
  }

  QuicByteCount SendStreamDataToPeer(QuicStreamId id,
                                     absl::string_view data,
                                     QuicStreamOffset offset,
                                     StreamSendingState state,
                                     QuicPacketNumber* last_packet) {
    QuicByteCount packet_size;
    // Save the last packet's size.
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(SaveArg<3>(&packet_size));
    connection_.SendStreamDataWithString(id, data, offset, state);
    if (last_packet != nullptr) {
      *last_packet = creator_->packet_number();
    }
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .Times(AnyNumber());
    return packet_size;
  }

  void SendAckPacketToPeer() {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    {
      QuicConnection::ScopedPacketFlusher flusher(&connection_);
      connection_.SendAck();
    }
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .Times(AnyNumber());
  }

  void SendRstStream(QuicStreamId id,
                     QuicRstStreamErrorCode error,
                     QuicStreamOffset bytes_written) {
    notifier_.WriteOrBufferRstStream(id, error, bytes_written);
    connection_.OnStreamReset(id, error);
  }

  void SendPing() { notifier_.WriteOrBufferPing(); }

  MessageStatus SendMessage(absl::string_view message) {
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
    return connection_.SendMessage(
        1,
        MakeSpan(connection_.helper()->GetStreamSendBufferAllocator(), message,
                 &storage),
        false);
  }

  void ProcessAckPacket(uint64_t packet_number, QuicAckFrame* frame) {
    if (packet_number > 1) {
      QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, packet_number - 1);
    } else {
      QuicPacketCreatorPeer::ClearPacketNumber(&peer_creator_);
    }
    ProcessFramePacket(QuicFrame(frame));
  }

  void ProcessAckPacket(QuicAckFrame* frame) {
    ProcessFramePacket(QuicFrame(frame));
  }

  void ProcessStopWaitingPacket(QuicStopWaitingFrame frame) {
    ProcessFramePacket(QuicFrame(frame));
  }

  size_t ProcessStopWaitingPacketAtLevel(uint64_t number,
                                         QuicStopWaitingFrame frame,
                                         EncryptionLevel /*level*/) {
    return ProcessFramePacketAtLevel(number, QuicFrame(frame),
                                     ENCRYPTION_ZERO_RTT);
  }

  void ProcessGoAwayPacket(QuicGoAwayFrame* frame) {
    ProcessFramePacket(QuicFrame(frame));
  }

  bool IsMissing(uint64_t number) {
    return IsAwaitingPacket(connection_.ack_frame(), QuicPacketNumber(number),
                            QuicPacketNumber());
  }

  std::unique_ptr<QuicPacket> ConstructPacket(const QuicPacketHeader& header,
                                              const QuicFrames& frames) {
    auto packet = BuildUnsizedDataPacket(&peer_framer_, header, frames);
    EXPECT_NE(nullptr, packet.get());
    return packet;
  }

  QuicPacketHeader ConstructPacketHeader(uint64_t number,
                                         EncryptionLevel level) {
    QuicPacketHeader header;
    if (peer_framer_.version().HasIetfInvariantHeader() &&
        level < ENCRYPTION_FORWARD_SECURE) {
      // Set long header type accordingly.
      header.version_flag = true;
      header.form = IETF_QUIC_LONG_HEADER_PACKET;
      header.long_packet_type = EncryptionlevelToLongHeaderType(level);
      if (QuicVersionHasLongHeaderLengths(
              peer_framer_.version().transport_version)) {
        header.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
        if (header.long_packet_type == INITIAL) {
          header.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
        }
      }
    }
    // Set connection_id to peer's in memory representation as this data packet
    // is created by peer_framer.
    if (peer_framer_.perspective() == Perspective::IS_SERVER) {
      header.source_connection_id = connection_id_;
      header.source_connection_id_included = connection_id_included_;
      header.destination_connection_id_included = CONNECTION_ID_ABSENT;
    } else {
      header.destination_connection_id = connection_id_;
      header.destination_connection_id_included = connection_id_included_;
    }
    if (peer_framer_.version().HasIetfInvariantHeader() &&
        peer_framer_.perspective() == Perspective::IS_SERVER) {
      header.destination_connection_id_included = CONNECTION_ID_ABSENT;
      if (header.version_flag) {
        header.source_connection_id = connection_id_;
        header.source_connection_id_included = CONNECTION_ID_PRESENT;
        if (GetParam().version.handshake_protocol == PROTOCOL_QUIC_CRYPTO &&
            header.long_packet_type == ZERO_RTT_PROTECTED) {
          header.nonce = &kTestDiversificationNonce;
        }
      }
    }
    header.packet_number_length = packet_number_length_;
    header.packet_number = QuicPacketNumber(number);
    return header;
  }

  std::unique_ptr<QuicPacket> ConstructDataPacket(uint64_t number,
                                                  bool has_stop_waiting,
                                                  EncryptionLevel level) {
    QuicPacketHeader header = ConstructPacketHeader(number, level);
    QuicFrames frames;
    frames.push_back(QuicFrame(frame1_));
    if (has_stop_waiting) {
      frames.push_back(QuicFrame(stop_waiting_));
    }
    return ConstructPacket(header, frames);
  }

  std::unique_ptr<SerializedPacket> ConstructProbingPacket() {
    peer_creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
    if (VersionHasIetfQuicFrames(version().transport_version)) {
      QuicPathFrameBuffer payload = {
          {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xfe}};
      return QuicPacketCreatorPeer::
          SerializePathChallengeConnectivityProbingPacket(&peer_creator_,
                                                          payload);
    }
    return QuicPacketCreatorPeer::SerializeConnectivityProbingPacket(
        &peer_creator_);
  }

  std::unique_ptr<QuicPacket> ConstructClosePacket(uint64_t number) {
    peer_creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
    QuicPacketHeader header;
    // Set connection_id to peer's in memory representation as this connection
    // close packet is created by peer_framer.
    if (peer_framer_.perspective() == Perspective::IS_SERVER) {
      header.source_connection_id = connection_id_;
      header.destination_connection_id_included = CONNECTION_ID_ABSENT;
      if (!peer_framer_.version().HasIetfInvariantHeader()) {
        header.source_connection_id_included = CONNECTION_ID_PRESENT;
      }
    } else {
      header.destination_connection_id = connection_id_;
      if (peer_framer_.version().HasIetfInvariantHeader()) {
        header.destination_connection_id_included = CONNECTION_ID_ABSENT;
      }
    }

    header.packet_number = QuicPacketNumber(number);

    QuicErrorCode kQuicErrorCode = QUIC_PEER_GOING_AWAY;
    QuicConnectionCloseFrame qccf(peer_framer_.transport_version(),
                                  kQuicErrorCode, NO_IETF_QUIC_ERROR, "",
                                  /*transport_close_frame_type=*/0);
    QuicFrames frames;
    frames.push_back(QuicFrame(&qccf));
    return ConstructPacket(header, frames);
  }

  QuicTime::Delta DefaultRetransmissionTime() {
    return QuicTime::Delta::FromMilliseconds(kDefaultRetransmissionTimeMs);
  }

  QuicTime::Delta DefaultDelayedAckTime() {
    return QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
  }

  const QuicStopWaitingFrame InitStopWaitingFrame(uint64_t least_unacked) {
    QuicStopWaitingFrame frame;
    frame.least_unacked = QuicPacketNumber(least_unacked);
    return frame;
  }

  // Construct a ack_frame that acks all packet numbers between 1 and
  // |largest_acked|, except |missing|.
  // REQUIRES: 1 <= |missing| < |largest_acked|
  QuicAckFrame ConstructAckFrame(uint64_t largest_acked, uint64_t missing) {
    return ConstructAckFrame(QuicPacketNumber(largest_acked),
                             QuicPacketNumber(missing));
  }

  QuicAckFrame ConstructAckFrame(QuicPacketNumber largest_acked,
                                 QuicPacketNumber missing) {
    if (missing == QuicPacketNumber(1)) {
      return InitAckFrame({{missing + 1, largest_acked + 1}});
    }
    return InitAckFrame(
        {{QuicPacketNumber(1), missing}, {missing + 1, largest_acked + 1}});
  }

  // Undo nacking a packet within the frame.
  void AckPacket(QuicPacketNumber arrived, QuicAckFrame* frame) {
    EXPECT_FALSE(frame->packets.Contains(arrived));
    frame->packets.Add(arrived);
  }

  void TriggerConnectionClose() {
    // Send an erroneous packet to close the connection.
    EXPECT_CALL(visitor_,
                OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
        .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));

    EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
    // Triggers a connection by receiving ACK of unsent packet.
    QuicAckFrame frame = InitAckFrame(10000);
    ProcessAckPacket(1, &frame);
    EXPECT_FALSE(QuicConnectionPeer::GetConnectionClosePacket(&connection_) ==
                 nullptr);
    EXPECT_EQ(1, connection_close_frame_count_);
    EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
                IsError(QUIC_INVALID_ACK_DATA));
  }

  void BlockOnNextWrite() {
    writer_->BlockOnNextWrite();
    EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AtLeast(1));
  }

  void SimulateNextPacketTooLarge() { writer_->SimulateNextPacketTooLarge(); }

  void AlwaysGetPacketTooLarge() { writer_->AlwaysGetPacketTooLarge(); }

  void SetWritePauseTimeDelta(QuicTime::Delta delta) {
    writer_->SetWritePauseTimeDelta(delta);
  }

  void CongestionBlockWrites() {
    EXPECT_CALL(*send_algorithm_, CanSend(_))
        .WillRepeatedly(testing::Return(false));
  }

  void CongestionUnblockWrites() {
    EXPECT_CALL(*send_algorithm_, CanSend(_))
        .WillRepeatedly(testing::Return(true));
  }

  void set_perspective(Perspective perspective) {
    connection_.set_perspective(perspective);
    if (perspective == Perspective::IS_SERVER) {
      connection_.set_can_truncate_connection_ids(true);
      QuicConnectionPeer::SetNegotiatedVersion(&connection_);
      connection_.OnSuccessfulVersionNegotiation();
    }
    QuicFramerPeer::SetPerspective(&peer_framer_,
                                   QuicUtils::InvertPerspective(perspective));
    peer_framer_.SetInitialObfuscators(TestConnectionId());
    for (EncryptionLevel level : {ENCRYPTION_ZERO_RTT, ENCRYPTION_HANDSHAKE,
                                  ENCRYPTION_FORWARD_SECURE}) {
      if (peer_framer_.HasEncrypterOfEncryptionLevel(level)) {
        peer_creator_.SetEncrypter(
            level, std::make_unique<NullEncrypter>(peer_framer_.perspective()));
      }
    }
  }

  void set_packets_between_probes_base(
      const QuicPacketCount packets_between_probes_base) {
    QuicConnectionPeer::ReInitializeMtuDiscoverer(
        &connection_, packets_between_probes_base,
        QuicPacketNumber(packets_between_probes_base));
  }

  bool IsDefaultTestConfiguration() {
    TestParams p = GetParam();
    return p.ack_response == AckResponse::kImmediate &&
           p.version == AllSupportedVersions()[0] && p.no_stop_waiting;
  }

  void TestConnectionCloseQuicErrorCode(QuicErrorCode expected_code) {
    // Not strictly needed for this test, but is commonly done.
    EXPECT_FALSE(QuicConnectionPeer::GetConnectionClosePacket(&connection_) ==
                 nullptr);
    const std::vector<QuicConnectionCloseFrame>& connection_close_frames =
        writer_->connection_close_frames();
    ASSERT_EQ(1u, connection_close_frames.size());

    EXPECT_THAT(connection_close_frames[0].quic_error_code,
                IsError(expected_code));

    if (!VersionHasIetfQuicFrames(version().transport_version)) {
      EXPECT_THAT(connection_close_frames[0].wire_error_code,
                  IsError(expected_code));
      EXPECT_EQ(GOOGLE_QUIC_CONNECTION_CLOSE,
                connection_close_frames[0].close_type);
      return;
    }

    QuicErrorCodeToIetfMapping mapping =
        QuicErrorCodeToTransportErrorCode(expected_code);

    if (mapping.is_transport_close) {
      // This Google QUIC Error Code maps to a transport close,
      EXPECT_EQ(IETF_QUIC_TRANSPORT_CONNECTION_CLOSE,
                connection_close_frames[0].close_type);
    } else {
      // This maps to an application close.
      EXPECT_EQ(IETF_QUIC_APPLICATION_CONNECTION_CLOSE,
                connection_close_frames[0].close_type);
    }
    EXPECT_EQ(mapping.error_code, connection_close_frames[0].wire_error_code);
  }

  void MtuDiscoveryTestInit() {
    set_perspective(Perspective::IS_SERVER);
    QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
    if (version().SupportsAntiAmplificationLimit()) {
      QuicConnectionPeer::SetAddressValidated(&connection_);
    }
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    peer_creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
    // QuicFramer::GetMaxPlaintextSize uses the smallest max plaintext size
    // across all encrypters. The initial encrypter used with IETF QUIC has a
    // 16-byte overhead, while the NullEncrypter used throughout this test has a
    // 12-byte overhead. This test tests behavior that relies on computing the
    // packet size correctly, so by unsetting the initial encrypter, we avoid
    // having a mismatch between the overheads for the encrypters used. In
    // non-test scenarios all encrypters used for a given connection have the
    // same overhead, either 12 bytes for ones using Google QUIC crypto, or 16
    // bytes for ones using TLS.
    connection_.SetEncrypter(ENCRYPTION_INITIAL, nullptr);
    // Prevent packets from being coalesced.
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
    EXPECT_TRUE(connection_.connected());
  }

  void PathProbeTestInit(Perspective perspective) {
    set_perspective(perspective);
    EXPECT_EQ(connection_.perspective(), perspective);
    if (perspective == Perspective::IS_SERVER) {
      QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
    }
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    peer_creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);
    // Prevent packets from being coalesced.
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
    if (version().SupportsAntiAmplificationLimit() &&
        perspective == Perspective::IS_SERVER) {
      QuicConnectionPeer::SetAddressValidated(&connection_);
    }
    // Clear direct_peer_address.
    QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
    // Clear effective_peer_address, it is the same as direct_peer_address for
    // this test.
    QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                                QuicSocketAddress());
    EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
    } else {
      EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
    }
    QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 2);
    ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress,
                                    kPeerAddress, ENCRYPTION_FORWARD_SECURE);
    EXPECT_EQ(kPeerAddress, connection_.peer_address());
    EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
  }

  void TestClientRetryHandling(bool invalid_retry_tag,
                               bool missing_original_id_in_config,
                               bool wrong_original_id_in_config,
                               bool missing_retry_id_in_config,
                               bool wrong_retry_id_in_config);

  QuicConnectionId connection_id_;
  QuicFramer framer_;

  MockSendAlgorithm* send_algorithm_;
  std::unique_ptr<MockLossAlgorithm> loss_algorithm_;
  MockClock clock_;
  MockRandom random_generator_;
  SimpleBufferAllocator buffer_allocator_;
  std::unique_ptr<TestConnectionHelper> helper_;
  std::unique_ptr<TestAlarmFactory> alarm_factory_;
  QuicFramer peer_framer_;
  QuicPacketCreator peer_creator_;
  std::unique_ptr<TestPacketWriter> writer_;
  TestConnection connection_;
  QuicPacketCreator* creator_;
  QuicSentPacketManager* manager_;
  StrictMock<MockQuicConnectionVisitor> visitor_;

  QuicStreamFrame frame1_;
  QuicStreamFrame frame2_;
  QuicCryptoFrame crypto_frame_;
  QuicAckFrame ack_;
  QuicStopWaitingFrame stop_waiting_;
  QuicPacketNumberLength packet_number_length_;
  QuicConnectionIdIncluded connection_id_included_;

  SimpleSessionNotifier notifier_;

  QuicConnectionCloseFrame saved_connection_close_frame_;
  int connection_close_frame_count_;
};

// Run all end to end tests with all supported versions.
INSTANTIATE_TEST_SUITE_P(QuicConnectionTests,
                         QuicConnectionTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

// These two tests ensure that the QuicErrorCode mapping works correctly.
// Both tests expect to see a Google QUIC close if not running IETF QUIC.
// If running IETF QUIC, the first will generate a transport connection
// close, the second an application connection close.
// The connection close codes for the two tests are manually chosen;
// they are expected to always map to transport- and application-
// closes, respectively. If that changes, new codes should be chosen.
TEST_P(QuicConnectionTest, CloseErrorCodeTestTransport) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  connection_.CloseConnection(
      IETF_QUIC_PROTOCOL_VIOLATION, "Should be transport close",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(IETF_QUIC_PROTOCOL_VIOLATION);
}

// Test that the IETF QUIC Error code mapping function works
// properly for application connection close codes.
TEST_P(QuicConnectionTest, CloseErrorCodeTestApplication) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  connection_.CloseConnection(
      QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE,
      "Should be application close",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE);
}

TEST_P(QuicConnectionTest, SelfAddressChangeAtClient) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());
  EXPECT_TRUE(connection_.connected());

  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_));
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_));
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  // Cause change in self_address.
  QuicIpAddress host;
  host.FromString("1.1.1.1");
  QuicSocketAddress self_address(host, 123);
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_));
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_));
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), self_address, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.connected());
}

TEST_P(QuicConnectionTest, SelfAddressChangeAtServer) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  EXPECT_TRUE(connection_.connected());

  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_));
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_));
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  // Cause change in self_address.
  QuicIpAddress host;
  host.FromString("1.1.1.1");
  QuicSocketAddress self_address(host, 123);
  EXPECT_CALL(visitor_, AllowSelfAddressChange()).WillOnce(Return(false));
  if (version().handshake_protocol == PROTOCOL_TLS1_3) {
    EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  }
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), self_address, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_ERROR_MIGRATING_ADDRESS);
}

TEST_P(QuicConnectionTest, AllowSelfAddressChangeToMappedIpv4AddressAtServer) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  EXPECT_TRUE(connection_.connected());

  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(3);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(3);
  }
  QuicIpAddress host;
  host.FromString("1.1.1.1");
  QuicSocketAddress self_address1(host, 443);
  connection_.SetSelfAddress(self_address1);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), self_address1,
                                  kPeerAddress, ENCRYPTION_INITIAL);
  // Cause self_address change to mapped Ipv4 address.
  QuicIpAddress host2;
  host2.FromString(
      absl::StrCat("::ffff:", connection_.self_address().host().ToString()));
  QuicSocketAddress self_address2(host2, connection_.self_address().port());
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), self_address2,
                                  kPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.connected());
  // self_address change back to Ipv4 address.
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), self_address1,
                                  kPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.connected());
}

TEST_P(QuicConnectionTest, ClientAddressChangeAndPacketReordered) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());

  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  }
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 5);
  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(),
                        /*port=*/23456);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress,
                                  kNewPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());

  // Decrease packet number to simulate out-of-order packets.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 4);
  // This is an old packet, do not migrate.
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, PeerPortChangeAtServer) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Prevent packets from being coalesced.
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  if (version().SupportsAntiAmplificationLimit()) {
    QuicConnectionPeer::SetAddressValidated(&connection_);
  }

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  QuicTime::Delta default_init_rtt = rtt_stats->initial_rtt();
  rtt_stats->set_initial_rtt(default_init_rtt * 2);
  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());

  QuicSentPacketManagerPeer::SetConsecutiveRtoCount(manager_, 1);
  EXPECT_EQ(1u, manager_->GetConsecutiveRtoCount());
  QuicSentPacketManagerPeer::SetConsecutiveTlpCount(manager_, 2);
  EXPECT_EQ(2u, manager_->GetConsecutiveTlpCount());

  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);
  EXPECT_CALL(visitor_, OnStreamFrame(_))
      .WillOnce(Invoke(
          [=]() { EXPECT_EQ(kPeerAddress, connection_.peer_address()); }))
      .WillOnce(Invoke([=]() {
        EXPECT_EQ((GetQuicReloadableFlag(quic_start_peer_migration_earlier) ||
                           !GetParam().version.HasIetfQuicFrames()
                       ? kNewPeerAddress
                       : kPeerAddress),
                  connection_.peer_address());
      }));
  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  // Process another packet with a different peer address on server side will
  // start connection migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(1);
  QuicFrames frames2;
  frames2.push_back(QuicFrame(frame2_));
  ProcessFramesPacketWithAddresses(frames2, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  // PORT_CHANGE shouldn't state change in sent packet manager.
  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());
  EXPECT_EQ(1u, manager_->GetConsecutiveRtoCount());
  EXPECT_EQ(2u, manager_->GetConsecutiveTlpCount());
  EXPECT_EQ(manager_->GetSendAlgorithm(), send_algorithm_);
  if (connection_.validate_client_address()) {
    EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());
    EXPECT_EQ(1u, connection_.GetStats().num_validated_peer_migration);
  }
}

TEST_P(QuicConnectionTest, PeerIpAddressChangeAtServer) {
  if (!connection_.validate_client_address()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Prevent packets from being coalesced.
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  QuicConnectionPeer::SetAddressValidated(&connection_);

  // Enable 5 RTO
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k5RTO);
  config.SetInitialReceivedConnectionOptions(connection_options);
  QuicConfigPeer::SetNegotiated(&config, true);
  QuicConfigPeer::SetReceivedOriginalConnectionId(&config,
                                                  connection_.connection_id());
  QuicConfigPeer::SetReceivedInitialSourceConnectionId(&config,
                                                       QuicConnectionId());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback4(), /*port=*/23456);
  EXPECT_CALL(visitor_, OnStreamFrame(_))
      .WillOnce(Invoke(
          [=]() { EXPECT_EQ(kPeerAddress, connection_.peer_address()); }))
      .WillOnce(Invoke([=]() {
        EXPECT_EQ((GetQuicReloadableFlag(quic_start_peer_migration_earlier) ||
                           !GetParam().version.HasIetfQuicFrames()
                       ? kNewPeerAddress
                       : kPeerAddress),
                  connection_.peer_address());
      }));
  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  // Send some data to make connection has packets in flight.
  connection_.SendStreamData3();
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_TRUE(connection_.BlackholeDetectionInProgress());

  // Process another packet with a different peer address on server side will
  // start connection migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE)).Times(1);
  // IETF QUIC send algorithm should be changed to a different object, so no
  // OnPacketSent() called on the old send algorithm.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, NO_RETRANSMITTABLE_DATA))
      .Times(0);
  // Do not propagate OnCanWrite() to session notifier.
  EXPECT_CALL(visitor_, OnCanWrite()).Times(AtLeast(1u));

  QuicFrames frames2;
  frames2.push_back(QuicFrame(frame2_));
  ProcessFramesPacketWithAddresses(frames2, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());
  EXPECT_FALSE(connection_.BlackholeDetectionInProgress());

  EXPECT_EQ(2u, writer_->packets_write_attempts());
  EXPECT_FALSE(writer_->path_challenge_frames().empty());
  QuicPathFrameBuffer payload =
      writer_->path_challenge_frames().front().data_buffer;
  EXPECT_NE(connection_.sent_packet_manager().GetSendAlgorithm(),
            send_algorithm_);
  // Switch to use the mock send algorithm.
  send_algorithm_ = new StrictMock<MockSendAlgorithm>();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
      .Times(AnyNumber())
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, InSlowStart()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, InRecovery()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, PopulateConnectionStats(_)).Times(AnyNumber());
  connection_.SetSendAlgorithm(send_algorithm_);

  // PATH_CHALLENGE is expanded upto the max packet size which may exceeds the
  // anti-amplification limit.
  EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(1u,
            connection_.GetStats().num_reverse_path_validtion_upon_migration);

  // Verify server is throttled by anti-amplification limit.
  connection_.SendCryptoDataWithString("foo", 0);
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Receiving an ACK to the packet sent after changing peer address doesn't
  // finish migration validation.
  QuicAckFrame ack_frame = InitAckFrame(2);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  ProcessFramePacketWithAddresses(QuicFrame(&ack_frame), kSelfAddress,
                                  kNewPeerAddress, ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());

  // Receiving PATH_RESPONSE should lift the anti-amplification limit.
  QuicFrames frames3;
  frames3.push_back(QuicFrame(new QuicPathResponseFrame(99, payload)));
  EXPECT_CALL(visitor_, MaybeSendAddressToken());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(testing::AtLeast(1u));
  ProcessFramesPacketWithAddresses(frames3, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());

  // Verify the anti-amplification limit is lifted by sending a packet larger
  // than the anti-amplification limit.
  connection_.SendCryptoDataWithString(std::string(1200, 'a'), 0);
  EXPECT_EQ(1u, connection_.GetStats().num_validated_peer_migration);
}

TEST_P(QuicConnectionTest, EffectivePeerAddressChangeAtServer) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  if (version().SupportsAntiAmplificationLimit()) {
    QuicConnectionPeer::SetAddressValidated(&connection_);
  }
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is different from direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  const QuicSocketAddress kEffectivePeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/43210);
  connection_.ReturnEffectivePeerAddressForNextPacket(kEffectivePeerAddress);

  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kEffectivePeerAddress, connection_.effective_peer_address());

  // Process another packet with the same direct peer address and different
  // effective peer address on server side will start connection migration.
  const QuicSocketAddress kNewEffectivePeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/54321);
  connection_.ReturnEffectivePeerAddressForNextPacket(kNewEffectivePeerAddress);
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(1);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewEffectivePeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(kPeerAddress, writer_->last_write_peer_address());
  if (connection_.validate_client_address()) {
    EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());
    EXPECT_EQ(1u, connection_.GetStats().num_validated_peer_migration);
  }

  // Process another packet with a different direct peer address and the same
  // effective peer address on server side will not start connection migration.
  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);
  connection_.ReturnEffectivePeerAddressForNextPacket(kNewEffectivePeerAddress);
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);

  if (!connection_.validate_client_address()) {
    // ack_frame is used to complete the migration started by the last packet,
    // we need to make sure a new migration does not start after the previous
    // one is completed.
    QuicAckFrame ack_frame = InitAckFrame(1);
    EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
    ProcessFramePacketWithAddresses(QuicFrame(&ack_frame), kSelfAddress,
                                    kNewPeerAddress, ENCRYPTION_INITIAL);
    EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
    EXPECT_EQ(kNewEffectivePeerAddress, connection_.effective_peer_address());
    EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());
  }

  // Process another packet with different direct peer address and different
  // effective peer address on server side will start connection migration.
  const QuicSocketAddress kNewerEffectivePeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/65432);
  const QuicSocketAddress kFinalPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/34567);
  connection_.ReturnEffectivePeerAddressForNextPacket(
      kNewerEffectivePeerAddress);
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(1);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress,
                                  kFinalPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_EQ(kFinalPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewerEffectivePeerAddress, connection_.effective_peer_address());
  if (connection_.validate_client_address()) {
    EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());
    EXPECT_EQ(send_algorithm_,
              connection_.sent_packet_manager().GetSendAlgorithm());
    EXPECT_EQ(2u, connection_.GetStats().num_validated_peer_migration);
  }

  // While the previous migration is ongoing, process another packet with the
  // same direct peer address and different effective peer address on server
  // side will start a new connection migration.
  const QuicSocketAddress kNewestEffectivePeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback4(), /*port=*/65430);
  connection_.ReturnEffectivePeerAddressForNextPacket(
      kNewestEffectivePeerAddress);
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE)).Times(1);
  if (!connection_.validate_client_address()) {
    EXPECT_CALL(*send_algorithm_, OnConnectionMigration()).Times(1);
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress,
                                  kFinalPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_EQ(kFinalPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewestEffectivePeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());
  if (connection_.validate_client_address()) {
    EXPECT_NE(send_algorithm_,
              connection_.sent_packet_manager().GetSendAlgorithm());
    EXPECT_EQ(kFinalPeerAddress, writer_->last_write_peer_address());
    EXPECT_FALSE(writer_->path_challenge_frames().empty());
    EXPECT_EQ(0u, connection_.GetStats()
                      .num_peer_migration_while_validating_default_path);
    EXPECT_TRUE(connection_.HasPendingPathValidation());
  }
}

TEST_P(QuicConnectionTest, ReversePathValidationFailureAtServer) {
  if (!connection_.validate_client_address()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Prevent packets from being coalesced.
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  QuicConnectionPeer::SetAddressValidated(&connection_);

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback4(), /*port=*/23456);
  EXPECT_CALL(visitor_, OnStreamFrame(_))
      .WillOnce(Invoke(
          [=]() { EXPECT_EQ(kPeerAddress, connection_.peer_address()); }))
      .WillOnce(Invoke([=]() {
        EXPECT_EQ((GetQuicReloadableFlag(quic_start_peer_migration_earlier) ||
                           !GetParam().version.HasIetfQuicFrames()
                       ? kNewPeerAddress
                       : kPeerAddress),
                  connection_.peer_address());
      }));
  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  // Process another packet with a different peer address on server side will
  // start connection migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE)).Times(1);
  // IETF QUIC send algorithm should be changed to a different object, so no
  // OnPacketSent() called on the old send algorithm.
  EXPECT_CALL(*send_algorithm_, OnConnectionMigration()).Times(0);

  QuicFrames frames2;
  frames2.push_back(QuicFrame(frame2_));
  ProcessFramesPacketWithAddresses(frames2, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_FALSE(writer_->path_challenge_frames().empty());
  EXPECT_NE(connection_.sent_packet_manager().GetSendAlgorithm(),
            send_algorithm_);
  EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());

  for (size_t i = 0; i < QuicPathValidator::kMaxRetryTimes; ++i) {
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(3 * kInitialRttMs));
    static_cast<TestAlarmFactory::TestAlarm*>(
        QuicPathValidatorPeer::retry_timer(
            QuicConnectionPeer::path_validator(&connection_)))
        ->Fire();
  }
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());

  // Advance the time so that the reverse path validation times out.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(3 * kInitialRttMs));
  static_cast<TestAlarmFactory::TestAlarm*>(
      QuicPathValidatorPeer::retry_timer(
          QuicConnectionPeer::path_validator(&connection_)))
      ->Fire();
  EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(connection_.sent_packet_manager().GetSendAlgorithm(),
            send_algorithm_);
}

TEST_P(QuicConnectionTest, ReceivePathProbeWithNoAddressChangeAtServer) {
  PathProbeTestInit(Perspective::IS_SERVER);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  EXPECT_CALL(visitor_, OnPacketReceived(_, _, false)).Times(0);

  // Process a padded PING packet with no peer address change on server side
  // will be ignored. But a PATH CHALLENGE packet with no peer address change
  // will be considered as path probing.
  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();

  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));

  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;
  ProcessReceivedPacket(kSelfAddress, kPeerAddress, *received);

  EXPECT_EQ(num_probing_received + (GetParam().version.HasIetfQuicFrames() &&
                                            connection_.send_path_response()
                                        ? 1u
                                        : 0u),
            connection_.GetStats().num_connectivity_probing_received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
}

// Regression test for b/150161358.
TEST_P(QuicConnectionTest, BufferedMtuPacketTooBig) {
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(1);
  writer_->SetWriteBlocked();

  // Send a MTU packet while blocked. It should be buffered.
  connection_.SendMtuDiscoveryPacket(kMaxOutgoingPacketSize);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
  EXPECT_TRUE(writer_->IsWriteBlocked());

  writer_->AlwaysGetPacketTooLarge();
  writer_->SetWritable();
  connection_.OnCanWrite();
}

TEST_P(QuicConnectionTest, WriteOutOfOrderQueuedPackets) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  set_perspective(Perspective::IS_CLIENT);

  BlockOnNextWrite();

  QuicStreamId stream_id = 2;
  connection_.SendStreamDataWithString(stream_id, "foo", 0, NO_FIN);

  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  writer_->SetWritable();
  connection_.SendConnectivityProbingPacket(writer_.get(),
                                            connection_.peer_address());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);
  connection_.OnCanWrite();
}

TEST_P(QuicConnectionTest, DiscardQueuedPacketsAfterConnectionClose) {
  // Regression test for b/74073386.
  {
    InSequence seq;
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .Times(AtLeast(1));
    EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(AtLeast(1));
  }

  set_perspective(Perspective::IS_CLIENT);

  writer_->SimulateNextPacketTooLarge();

  // This packet write should fail, which should cause the connection to close
  // after sending a connection close packet, then the failed packet should be
  // queued.
  connection_.SendStreamDataWithString(/*id=*/2, "foo", 0, NO_FIN);

  EXPECT_FALSE(connection_.connected());
  // No need to buffer packets.
  EXPECT_EQ(0u, connection_.NumQueuedPackets());

  EXPECT_EQ(0u, connection_.GetStats().packets_discarded);
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.GetStats().packets_discarded);
}

class TestQuicPathValidationContext : public QuicPathValidationContext {
 public:
  TestQuicPathValidationContext(const QuicSocketAddress& self_address,
                                const QuicSocketAddress& peer_address,

                                QuicPacketWriter* writer)
      : QuicPathValidationContext(self_address, peer_address),
        writer_(writer) {}

  QuicPacketWriter* WriterToUse() override { return writer_; }

 private:
  QuicPacketWriter* writer_;
};

class TestValidationResultDelegate : public QuicPathValidator::ResultDelegate {
 public:
  TestValidationResultDelegate(const QuicSocketAddress& expected_self_address,
                               const QuicSocketAddress& expected_peer_address,
                               bool* success)
      : QuicPathValidator::ResultDelegate(),
        expected_self_address_(expected_self_address),
        expected_peer_address_(expected_peer_address),
        success_(success) {}
  void OnPathValidationSuccess(
      std::unique_ptr<QuicPathValidationContext> context) override {
    EXPECT_EQ(expected_self_address_, context->self_address());
    EXPECT_EQ(expected_peer_address_, context->peer_address());
    *success_ = true;
  }

  void OnPathValidationFailure(
      std::unique_ptr<QuicPathValidationContext> context) override {
    EXPECT_EQ(expected_self_address_, context->self_address());
    EXPECT_EQ(expected_peer_address_, context->peer_address());
    *success_ = false;
  }

 private:
  QuicSocketAddress expected_self_address_;
  QuicSocketAddress expected_peer_address_;
  bool* success_;
};

// Receive a path probe request at the server side, i.e.,
// in non-IETF version: receive a padded PING packet with a peer addess change;
// in IETF version: receive a packet contains PATH CHALLENGE with peer address
// change.
TEST_P(QuicConnectionTest, ReceivePathProbingAtServer) {
  PathProbeTestInit(Perspective::IS_SERVER);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  QuicPathFrameBuffer payload;
  if (!GetParam().version.HasIetfQuicFrames()) {
    EXPECT_CALL(visitor_,
                OnPacketReceived(_, _, /*is_connectivity_probe=*/true))
        .Times(1);
  } else {
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, _)).Times(0);
    if (connection_.validate_client_address()) {
      EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
          .Times(AtLeast(1u))
          .WillOnce(Invoke([&]() {
            EXPECT_EQ(1u, writer_->path_challenge_frames().size());
            EXPECT_EQ(1u, writer_->path_response_frames().size());
            payload = writer_->path_challenge_frames().front().data_buffer;
          }));
    }
  }
  // Process a probing packet from a new peer address on server side
  // is effectively receiving a connectivity probing.
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback4(),
                                          /*port=*/23456);

  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));
  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;
  ProcessReceivedPacket(kSelfAddress, kNewPeerAddress, *received);

  EXPECT_EQ(num_probing_received + 1,
            connection_.GetStats().num_connectivity_probing_received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
  if (GetParam().version.HasIetfQuicFrames() &&
      connection_.use_path_validator() &&
      GetQuicReloadableFlag(quic_count_bytes_on_alternative_path_seperately)) {
    QuicByteCount bytes_sent =
        QuicConnectionPeer::BytesSentOnAlternativePath(&connection_);
    EXPECT_LT(0u, bytes_sent);
    EXPECT_EQ(received->length(),
              QuicConnectionPeer::BytesReceivedOnAlternativePath(&connection_));

    // Receiving one more probing packet should update the bytes count.
    probing_packet = ConstructProbingPacket();
    received.reset(ConstructReceivedPacket(
        QuicEncryptedPacket(probing_packet->encrypted_buffer,
                            probing_packet->encrypted_length),
        clock_.Now()));
    ProcessReceivedPacket(kSelfAddress, kNewPeerAddress, *received);

    EXPECT_EQ(num_probing_received + 2,
              connection_.GetStats().num_connectivity_probing_received);
    EXPECT_EQ(2 * bytes_sent,
              QuicConnectionPeer::BytesSentOnAlternativePath(&connection_));
    EXPECT_EQ(2 * received->length(),
              QuicConnectionPeer::BytesReceivedOnAlternativePath(&connection_));

    bool success = false;
    if (!connection_.validate_client_address()) {
      EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
          .Times(AtLeast(1u))
          .WillOnce(Invoke([&]() {
            EXPECT_EQ(1u, writer_->path_challenge_frames().size());
            payload = writer_->path_challenge_frames().front().data_buffer;
          }));

      connection_.ValidatePath(
          std::make_unique<TestQuicPathValidationContext>(
              connection_.self_address(), kNewPeerAddress, writer_.get()),
          std::make_unique<TestValidationResultDelegate>(
              connection_.self_address(), kNewPeerAddress, &success));
    }
    EXPECT_EQ((connection_.validate_client_address() ? 2 : 3) * bytes_sent,
              QuicConnectionPeer::BytesSentOnAlternativePath(&connection_));
    QuicFrames frames;
    frames.push_back(QuicFrame(new QuicPathResponseFrame(99, payload)));
    ProcessFramesPacketWithAddresses(frames, connection_.self_address(),
                                     kNewPeerAddress,
                                     ENCRYPTION_FORWARD_SECURE);
    EXPECT_LT(2 * received->length(),
              QuicConnectionPeer::BytesReceivedOnAlternativePath(&connection_));
    EXPECT_TRUE(QuicConnectionPeer::IsAlternativePathValidated(&connection_));

    // Receiving another probing packet from a newer address with a different
    // port shouldn't trigger another reverse path validation.
    QuicSocketAddress kNewerPeerAddress(QuicIpAddress::Loopback4(),
                                        /*port=*/34567);
    probing_packet = ConstructProbingPacket();
    received.reset(ConstructReceivedPacket(
        QuicEncryptedPacket(probing_packet->encrypted_buffer,
                            probing_packet->encrypted_length),
        clock_.Now()));
    ProcessReceivedPacket(kSelfAddress, kNewerPeerAddress, *received);
    EXPECT_FALSE(connection_.HasPendingPathValidation());
    EXPECT_EQ(connection_.validate_client_address(),
              QuicConnectionPeer::IsAlternativePathValidated(&connection_));
  }

  // Process another packet with the old peer address on server side will not
  // start peer migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
}

// Receive a padded PING packet with a port change on server side.
TEST_P(QuicConnectionTest, ReceivePaddedPingWithPortChangeAtServer) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  if (version().SupportsAntiAmplificationLimit()) {
    QuicConnectionPeer::SetAddressValidated(&connection_);
  }

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

  if (GetParam().version.UsesCryptoFrames()) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  if (GetParam().version.HasIetfQuicFrames()) {
    // In IETF version, a padded PING packet with port change is not taken as
    // connectivity probe.
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
    EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(1);
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, _)).Times(0);
  } else {
    // In non-IETF version, process a padded PING packet from a new peer
    // address on server side is effectively receiving a connectivity probing.
    EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
    EXPECT_CALL(visitor_,
                OnPacketReceived(_, _, /*is_connectivity_probe=*/true))
        .Times(1);
  }
  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);

  QuicFrames frames;
  // Write a PING frame, which has no data payload.
  QuicPingFrame ping_frame;
  frames.push_back(QuicFrame(ping_frame));

  // Add padding to the rest of the packet.
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(padding_frame));

  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;

  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_INITIAL);

  if (GetParam().version.HasIetfQuicFrames()) {
    // Padded PING with port changen is not considered as connectivity probe but
    // a PORT CHANGE.
    EXPECT_EQ(num_probing_received,
              connection_.GetStats().num_connectivity_probing_received);
    EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
    EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  } else {
    EXPECT_EQ(num_probing_received + 1,
              connection_.GetStats().num_connectivity_probing_received);
    EXPECT_EQ(kPeerAddress, connection_.peer_address());
    EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
  }

  if (GetParam().version.HasIetfQuicFrames()) {
    EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(1);
  }
  // Process another packet with the old peer address on server side. gQUIC
  // shouldn't regard this as a peer migration.
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, ReceiveReorderedPathProbingAtServer) {
  PathProbeTestInit(Perspective::IS_SERVER);

  // Decrease packet number to simulate out-of-order packets.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 4);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  if (!GetParam().version.HasIetfQuicFrames()) {
    EXPECT_CALL(visitor_,
                OnPacketReceived(_, _, /*is_connectivity_probe=*/true))
        .Times(1);
  } else {
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, _)).Times(0);
  }

  // Process a padded PING packet from a new peer address on server side
  // is effectively receiving a connectivity probing, even if a newer packet has
  // been received before this one.
  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);

  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));

  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;
  ProcessReceivedPacket(kSelfAddress, kNewPeerAddress, *received);

  EXPECT_EQ(num_probing_received + 1,
            connection_.GetStats().num_connectivity_probing_received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, MigrateAfterProbingAtServer) {
  PathProbeTestInit(Perspective::IS_SERVER);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  if (!GetParam().version.HasIetfQuicFrames()) {
    EXPECT_CALL(visitor_,
                OnPacketReceived(_, _, /*is_connectivity_probe=*/true))
        .Times(1);
  } else {
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, _)).Times(0);
  }

  // Process a padded PING packet from a new peer address on server side
  // is effectively receiving a connectivity probing.
  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);

  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));
  ProcessReceivedPacket(kSelfAddress, kNewPeerAddress, *received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  // Process another non-probing packet with the new peer address on server
  // side will start peer migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(1);

  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress,
                                  kNewPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, ReceiveConnectivityProbingPacketAtClient) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  PathProbeTestInit(Perspective::IS_CLIENT);

  // Client takes all padded PING packet as speculative connectivity
  // probing packet, and reports to visitor.
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  if (!connection_.send_path_response()) {
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, false)).Times(1);
  }

  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));
  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;
  ProcessReceivedPacket(kSelfAddress, kPeerAddress, *received);

  EXPECT_EQ(num_probing_received + (GetParam().version.HasIetfQuicFrames() &&
                                            connection_.send_path_response()
                                        ? 1u
                                        : 0u),
            connection_.GetStats().num_connectivity_probing_received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, ReceiveConnectivityProbingResponseAtClient) {
  // TODO(b/150095484): add test coverage for IETF to verify that client takes
  // PATH RESPONSE with peer address change as correct validation on the new
  // path.
  if (GetParam().version.HasIetfQuicFrames()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  PathProbeTestInit(Perspective::IS_CLIENT);

  // Process a padded PING packet with a different self address on client side
  // is effectively receiving a connectivity probing.
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  if (!GetParam().version.HasIetfQuicFrames()) {
    EXPECT_CALL(visitor_,
                OnPacketReceived(_, _, /*is_connectivity_probe=*/true))
        .Times(1);
  } else {
    EXPECT_CALL(visitor_, OnPacketReceived(_, _, _)).Times(0);
  }

  const QuicSocketAddress kNewSelfAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);

  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));
  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;
  ProcessReceivedPacket(kNewSelfAddress, kPeerAddress, *received);

  EXPECT_EQ(num_probing_received + 1,
            connection_.GetStats().num_connectivity_probing_received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, PeerAddressChangeAtClient) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  set_perspective(Perspective::IS_CLIENT);
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  // Process another packet with a different peer address on client side will
  // only update peer address.
  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress,
                                  kNewPeerAddress, ENCRYPTION_INITIAL);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
}

TEST_P(QuicConnectionTest, MaxPacketSize) {
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());
  EXPECT_EQ(1350u, connection_.max_packet_length());
}

TEST_P(QuicConnectionTest, PeerLowersMaxPacketSize) {
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());

  // SetFromConfig is always called after construction from InitializeSession.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  constexpr uint32_t kTestMaxPacketSize = 1233u;
  QuicConfig config;
  QuicConfigPeer::SetReceivedMaxPacketSize(&config, kTestMaxPacketSize);
  connection_.SetFromConfig(config);

  EXPECT_EQ(kTestMaxPacketSize, connection_.max_packet_length());
}

TEST_P(QuicConnectionTest, PeerCannotRaiseMaxPacketSize) {
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());

  // SetFromConfig is always called after construction from InitializeSession.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  constexpr uint32_t kTestMaxPacketSize = 1450u;
  QuicConfig config;
  QuicConfigPeer::SetReceivedMaxPacketSize(&config, kTestMaxPacketSize);
  connection_.SetFromConfig(config);

  EXPECT_EQ(kDefaultMaxPacketSize, connection_.max_packet_length());
}

TEST_P(QuicConnectionTest, SmallerServerMaxPacketSize) {
  TestConnection connection(TestConnectionId(), kSelfAddress, kPeerAddress,
                            helper_.get(), alarm_factory_.get(), writer_.get(),
                            Perspective::IS_SERVER, version());
  EXPECT_EQ(Perspective::IS_SERVER, connection.perspective());
  EXPECT_EQ(1000u, connection.max_packet_length());
}

TEST_P(QuicConnectionTest, IncreaseServerMaxPacketSize) {
  set_perspective(Perspective::IS_SERVER);
  connection_.SetMaxPacketLength(1000);

  QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.version_flag = true;
  header.packet_number = QuicPacketNumber(12);

  if (QuicVersionHasLongHeaderLengths(
          peer_framer_.version().transport_version)) {
    header.long_packet_type = INITIAL;
    header.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  QuicFrames frames;
  QuicPaddingFrame padding;
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    frames.push_back(QuicFrame(&crypto_frame_));
  } else {
    frames.push_back(QuicFrame(frame1_));
  }
  frames.push_back(QuicFrame(padding));
  std::unique_ptr<QuicPacket> packet(ConstructPacket(header, frames));
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      peer_framer_.EncryptPayload(ENCRYPTION_INITIAL, QuicPacketNumber(12),
                                  *packet, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(kMaxOutgoingPacketSize, encrypted_length);

  framer_.set_version(version());
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  }
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, encrypted_length, QuicTime::Zero(), false));

  EXPECT_EQ(kMaxOutgoingPacketSize, connection_.max_packet_length());
}

TEST_P(QuicConnectionTest, IncreaseServerMaxPacketSizeWhileWriterLimited) {
  const QuicByteCount lower_max_packet_size = 1240;
  writer_->set_max_packet_size(lower_max_packet_size);
  set_perspective(Perspective::IS_SERVER);
  connection_.SetMaxPacketLength(1000);
  EXPECT_EQ(1000u, connection_.max_packet_length());

  QuicPacketHeader header;
  header.destination_connection_id = connection_id_;
  header.version_flag = true;
  header.packet_number = QuicPacketNumber(12);

  if (QuicVersionHasLongHeaderLengths(
          peer_framer_.version().transport_version)) {
    header.long_packet_type = INITIAL;
    header.retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_1;
    header.length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }

  QuicFrames frames;
  QuicPaddingFrame padding;
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    frames.push_back(QuicFrame(&crypto_frame_));
  } else {
    frames.push_back(QuicFrame(frame1_));
  }
  frames.push_back(QuicFrame(padding));
  std::unique_ptr<QuicPacket> packet(ConstructPacket(header, frames));
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      peer_framer_.EncryptPayload(ENCRYPTION_INITIAL, QuicPacketNumber(12),
                                  *packet, buffer, kMaxOutgoingPacketSize);
  EXPECT_EQ(kMaxOutgoingPacketSize, encrypted_length);

  framer_.set_version(version());
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  }
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, encrypted_length, QuicTime::Zero(), false));

  // Here, the limit imposed by the writer is lower than the size of the packet
  // received, so the writer max packet size is used.
  EXPECT_EQ(lower_max_packet_size, connection_.max_packet_length());
}

TEST_P(QuicConnectionTest, LimitMaxPacketSizeByWriter) {
  const QuicByteCount lower_max_packet_size = 1240;
  writer_->set_max_packet_size(lower_max_packet_size);

  static_assert(lower_max_packet_size < kDefaultMaxPacketSize,
                "Default maximum packet size is too low");
  connection_.SetMaxPacketLength(kDefaultMaxPacketSize);

  EXPECT_EQ(lower_max_packet_size, connection_.max_packet_length());
}

TEST_P(QuicConnectionTest, LimitMaxPacketSizeByWriterForNewConnection) {
  const QuicConnectionId connection_id = TestConnectionId(17);
  const QuicByteCount lower_max_packet_size = 1240;
  writer_->set_max_packet_size(lower_max_packet_size);
  TestConnection connection(connection_id, kSelfAddress, kPeerAddress,
                            helper_.get(), alarm_factory_.get(), writer_.get(),
                            Perspective::IS_CLIENT, version());
  EXPECT_EQ(Perspective::IS_CLIENT, connection.perspective());
  EXPECT_EQ(lower_max_packet_size, connection.max_packet_length());
}

TEST_P(QuicConnectionTest, PacketsInOrder) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  ProcessPacket(1);
  EXPECT_EQ(QuicPacketNumber(1u), LargestAcked(connection_.ack_frame()));
  EXPECT_EQ(1u, connection_.ack_frame().packets.NumIntervals());

  ProcessPacket(2);
  EXPECT_EQ(QuicPacketNumber(2u), LargestAcked(connection_.ack_frame()));
  EXPECT_EQ(1u, connection_.ack_frame().packets.NumIntervals());

  ProcessPacket(3);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_EQ(1u, connection_.ack_frame().packets.NumIntervals());
}

TEST_P(QuicConnectionTest, PacketsOutOfOrder) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  ProcessPacket(3);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(2);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_FALSE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(1);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_FALSE(IsMissing(2));
  EXPECT_FALSE(IsMissing(1));
}

TEST_P(QuicConnectionTest, DuplicatePacket) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  ProcessPacket(3);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  // Send packet 3 again, but do not set the expectation that
  // the visitor OnStreamFrame() will be called.
  ProcessDataPacket(3);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));
}

TEST_P(QuicConnectionTest, PacketsOutOfOrderWithAdditionsAndLeastAwaiting) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  ProcessPacket(3);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(2);
  EXPECT_EQ(QuicPacketNumber(3u), LargestAcked(connection_.ack_frame()));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(5);
  EXPECT_EQ(QuicPacketNumber(5u), LargestAcked(connection_.ack_frame()));
  EXPECT_TRUE(IsMissing(1));
  EXPECT_TRUE(IsMissing(4));

  // Pretend at this point the client has gotten acks for 2 and 3 and 1 is a
  // packet the peer will not retransmit.  It indicates this by sending 'least
  // awaiting' is 4.  The connection should then realize 1 will not be
  // retransmitted, and will remove it from the missing list.
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  ProcessAckPacket(6, &frame);

  // Force an ack to be sent.
  SendAckPacketToPeer();
  EXPECT_TRUE(IsMissing(4));
}

TEST_P(QuicConnectionTest, RejectUnencryptedStreamData) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  // Process an unencrypted packet from the non-crypto stream.
  frame1_.stream_id = 3;
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_QUIC_PEER_BUG(ProcessDataPacketAtLevel(1, false, ENCRYPTION_INITIAL),
                       "");
  TestConnectionCloseQuicErrorCode(QUIC_UNENCRYPTED_STREAM_DATA);
}

TEST_P(QuicConnectionTest, OutOfOrderReceiptCausesAckSend) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  ProcessPacket(3);
  // Should not cause an ack.
  EXPECT_EQ(0u, writer_->packets_write_attempts());

  ProcessPacket(2);
  // Should ack immediately, since this fills the last hole.
  EXPECT_EQ(1u, writer_->packets_write_attempts());

  ProcessPacket(1);
  // Should ack immediately, since this fills the last hole.
  EXPECT_EQ(2u, writer_->packets_write_attempts());

  ProcessPacket(4);
  // Should not cause an ack.
  EXPECT_EQ(2u, writer_->packets_write_attempts());
}

TEST_P(QuicConnectionTest, OutOfOrderAckReceiptCausesNoAck) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  SendStreamDataToPeer(1, "foo", 0, NO_FIN, nullptr);
  SendStreamDataToPeer(1, "bar", 3, NO_FIN, nullptr);
  EXPECT_EQ(2u, writer_->packets_write_attempts());

  QuicAckFrame ack1 = InitAckFrame(1);
  QuicAckFrame ack2 = InitAckFrame(2);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    EXPECT_CALL(visitor_, OnOneRttPacketAcknowledged()).Times(1);
  }
  ProcessAckPacket(2, &ack2);
  // Should ack immediately since we have missing packets.
  EXPECT_EQ(2u, writer_->packets_write_attempts());

  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    EXPECT_CALL(visitor_, OnOneRttPacketAcknowledged()).Times(0);
  }
  ProcessAckPacket(1, &ack1);
  // Should not ack an ack filling a missing packet.
  EXPECT_EQ(2u, writer_->packets_write_attempts());
}

TEST_P(QuicConnectionTest, AckReceiptCausesAckSend) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  QuicPacketNumber original, second;

  QuicByteCount packet_size =
      SendStreamDataToPeer(3, "foo", 0, NO_FIN, &original);  // 1st packet.
  SendStreamDataToPeer(3, "bar", 3, NO_FIN, &second);        // 2nd packet.

  QuicAckFrame frame = InitAckFrame({{second, second + 1}});
  // First nack triggers early retransmit.
  LostPacketVector lost_packets;
  lost_packets.push_back(LostPacket(original, kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicPacketNumber retransmission;
  // Packet 1 is short header for IETF QUIC because the encryption level
  // switched to ENCRYPTION_FORWARD_SECURE in SendStreamDataToPeer.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _,
                           GetParam().version.HasIetfInvariantHeader()
                               ? packet_size
                               : packet_size - kQuicVersionSize,
                           _))
      .WillOnce(SaveArg<2>(&retransmission));

  ProcessAckPacket(&frame);

  QuicAckFrame frame2 = ConstructAckFrame(retransmission, original);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  ProcessAckPacket(&frame2);

  // Now if the peer sends an ack which still reports the retransmitted packet
  // as missing, that will bundle an ack with data after two acks in a row
  // indicate the high water mark needs to be raised.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, HAS_RETRANSMITTABLE_DATA));
  connection_.SendStreamDataWithString(3, "foo", 6, NO_FIN);
  // No ack sent.
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->stream_frames().size());

  // No more packet loss for the rest of the test.
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .Times(AnyNumber());
  ProcessAckPacket(&frame2);
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, HAS_RETRANSMITTABLE_DATA));
  connection_.SendStreamDataWithString(3, "foofoofoo", 9, NO_FIN);
  // Ack bundled.
  if (GetParam().no_stop_waiting) {
    // Do not ACK acks.
    EXPECT_EQ(1u, writer_->frame_count());
  } else {
    EXPECT_EQ(3u, writer_->frame_count());
  }
  EXPECT_EQ(1u, writer_->stream_frames().size());
  if (GetParam().no_stop_waiting) {
    EXPECT_TRUE(writer_->ack_frames().empty());
  } else {
    EXPECT_FALSE(writer_->ack_frames().empty());
  }

  // But an ack with no missing packets will not send an ack.
  AckPacket(original, &frame2);
  ProcessAckPacket(&frame2);
  ProcessAckPacket(&frame2);
}

TEST_P(QuicConnectionTest, AckFrequencyUpdatedFromAckFrequencyFrame) {
  if (!GetParam().version.HasIetfQuicFrames()) {
    return;
  }
  connection_.set_can_receive_ack_frequency_frame();

  // Expect 13 acks, every 3rd packet including the first packet with
  // AckFrequencyFrame.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(13);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  QuicAckFrequencyFrame ack_frequency_frame;
  ack_frequency_frame.packet_tolerance = 3;
  ProcessFramePacketAtLevel(1, QuicFrame(&ack_frequency_frame),
                            ENCRYPTION_FORWARD_SECURE);

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(38);
  // Receives packets 2 - 39.
  for (size_t i = 2; i <= 39; ++i) {
    ProcessDataPacket(i);
  }
}

TEST_P(QuicConnectionTest,
       AckFrequencyFrameOutsideApplicationDataNumberSpaceIsIgnored) {
  if (!GetParam().version.HasIetfQuicFrames()) {
    return;
  }
  connection_.set_can_receive_ack_frequency_frame();

  QuicAckFrequencyFrame ack_frequency_frame;
  ack_frequency_frame.packet_tolerance = 3;
  ProcessFramePacketAtLevel(1, QuicFrame(&ack_frequency_frame),
                            ENCRYPTION_HANDSHAKE);

  // Expect 30 acks, every 2nd (instead of 3rd) packet including the first
  // packet with AckFrequencyFrame.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(30);
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(60);
  // Receives packets 2 - 61.
  for (size_t i = 2; i <= 61; ++i) {
    ProcessDataPacket(i);
  }
}

TEST_P(QuicConnectionTest, AckDecimationReducesAcks) {
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame()).Times(AnyNumber());

  // Start ack decimation from 10th packet.
  connection_.set_min_received_before_ack_decimation(10);

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(30);

  // Expect 6 acks: 5 acks between packets 1-10, and ack at 20.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(6);
  // Receives packets 1 - 29.
  for (size_t i = 1; i <= 29; ++i) {
    ProcessDataPacket(i);
  }

  // We now receive the 30th packet, and so we send an ack.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessDataPacket(30);
}

TEST_P(QuicConnectionTest, AckNeedsRetransmittableFrames) {
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(99);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(19);
  // Receives packets 1 - 39.
  for (size_t i = 1; i <= 39; ++i) {
    ProcessDataPacket(i);
  }
  // Receiving Packet 40 causes 20th ack to send. Session is informed and adds
  // WINDOW_UPDATE.
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame())
      .WillOnce(Invoke([this]() {
        connection_.SendControlFrame(
            QuicFrame(new QuicWindowUpdateFrame(1, 0, 0)));
      }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_EQ(0u, writer_->window_update_frames().size());
  ProcessDataPacket(40);
  EXPECT_EQ(1u, writer_->window_update_frames().size());

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(9);
  // Receives packets 41 - 59.
  for (size_t i = 41; i <= 59; ++i) {
    ProcessDataPacket(i);
  }
  // Send a packet containing stream frame.
  SendStreamDataToPeer(
      QuicUtils::GetFirstBidirectionalStreamId(
          connection_.version().transport_version, Perspective::IS_CLIENT),
      "bar", 0, NO_FIN, nullptr);

  // Session will not be informed until receiving another 20 packets.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(19);
  for (size_t i = 60; i <= 98; ++i) {
    ProcessDataPacket(i);
    EXPECT_EQ(0u, writer_->window_update_frames().size());
  }
  // Session does not add a retransmittable frame.
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame())
      .WillOnce(Invoke([this]() {
        connection_.SendControlFrame(QuicFrame(QuicPingFrame(1)));
      }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_EQ(0u, writer_->ping_frames().size());
  ProcessDataPacket(99);
  EXPECT_EQ(0u, writer_->window_update_frames().size());
  // A ping frame will be added.
  EXPECT_EQ(1u, writer_->ping_frames().size());
}

TEST_P(QuicConnectionTest, AckNeedsRetransmittableFramesAfterPto) {
  // Disable TLP so the RTO fires immediately.
  connection_.SetMaxTailLossProbes(0);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kEACK);
  config.SetConnectionOptionsToSend(connection_options);
  connection_.SetFromConfig(config);

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.OnHandshakeComplete();

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(10);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(4);
  // Receive packets 1 - 9.
  for (size_t i = 1; i <= 9; ++i) {
    ProcessDataPacket(i);
  }

  // Send a ping and fire the retransmission alarm.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  SendPing();
  QuicTime retransmission_time =
      connection_.GetRetransmissionAlarm()->deadline();
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  connection_.GetRetransmissionAlarm()->Fire();
  ASSERT_TRUE(manager_->GetConsecutiveRtoCount() > 0 ||
              manager_->GetConsecutivePtoCount() > 0);

  // Process a packet, which requests a retransmittable frame be bundled
  // with the ACK.
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame())
      .WillOnce(Invoke([this]() {
        connection_.SendControlFrame(
            QuicFrame(new QuicWindowUpdateFrame(1, 0, 0)));
      }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessDataPacket(11);
  EXPECT_EQ(1u, writer_->window_update_frames().size());
}

TEST_P(QuicConnectionTest, LeastUnackedLower) {
  if (GetParam().version.HasIetfInvariantHeader()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  SendStreamDataToPeer(1, "foo", 0, NO_FIN, nullptr);
  SendStreamDataToPeer(1, "bar", 3, NO_FIN, nullptr);
  SendStreamDataToPeer(1, "eep", 6, NO_FIN, nullptr);

  // Start out saying the least unacked is 2.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 5);
  ProcessStopWaitingPacket(InitStopWaitingFrame(2));

  // Change it to 1, but lower the packet number to fake out-of-order packets.
  // This should be fine.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 1);
  // The scheduler will not process out of order acks, but all packet processing
  // causes the connection to try to write.
  if (!GetParam().no_stop_waiting) {
    EXPECT_CALL(visitor_, OnCanWrite());
  }
  ProcessStopWaitingPacket(InitStopWaitingFrame(1));

  // Now claim it's one, but set the ordering so it was sent "after" the first
  // one.  This should cause a connection error.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 7);
  if (!GetParam().no_stop_waiting) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .Times(AtLeast(1));
    EXPECT_CALL(visitor_,
                OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
        .Times(AtLeast(1));
  }
  ProcessStopWaitingPacket(InitStopWaitingFrame(1));
  if (!GetParam().no_stop_waiting) {
    TestConnectionCloseQuicErrorCode(QUIC_INVALID_STOP_WAITING_DATA);
  }
}

TEST_P(QuicConnectionTest, TooManySentPackets) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  QuicPacketCount max_tracked_packets = 50;
  QuicConnectionPeer::SetMaxTrackedPackets(&connection_, max_tracked_packets);

  const int num_packets = max_tracked_packets + 5;

  for (int i = 0; i < num_packets; ++i) {
    SendStreamDataToPeer(1, "foo", 3 * i, NO_FIN, nullptr);
  }

  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));

  ProcessFramePacket(QuicFrame(QuicPingFrame()));

  if (!GetQuicReloadableFlag(
          quic_close_connection_with_too_many_outstanding_packets)) {
    // When the flag is false, the ping packet processed above shouldn't cause
    // the connection to close. But the ack packet below will.
    EXPECT_TRUE(connection_.connected());

    // Ack packet 1, which leaves more than the limit outstanding.
    EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));

    // Nack the first packet and ack the rest, leaving a huge gap.
    QuicAckFrame frame1 = ConstructAckFrame(num_packets, 1);
    ProcessAckPacket(&frame1);
  }

  TestConnectionCloseQuicErrorCode(QUIC_TOO_MANY_OUTSTANDING_SENT_PACKETS);
}

TEST_P(QuicConnectionTest, LargestObservedLower) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  SendStreamDataToPeer(1, "foo", 0, NO_FIN, nullptr);
  SendStreamDataToPeer(1, "bar", 3, NO_FIN, nullptr);
  SendStreamDataToPeer(1, "eep", 6, NO_FIN, nullptr);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));

  // Start out saying the largest observed is 2.
  QuicAckFrame frame1 = InitAckFrame(1);
  QuicAckFrame frame2 = InitAckFrame(2);
  ProcessAckPacket(&frame2);

  EXPECT_CALL(visitor_, OnCanWrite());
  ProcessAckPacket(&frame1);
}

TEST_P(QuicConnectionTest, AckUnsentData) {
  // Ack a packet which has not been sent.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(visitor_, OnCanWrite()).Times(0);
  ProcessAckPacket(&frame);
  TestConnectionCloseQuicErrorCode(QUIC_INVALID_ACK_DATA);
}

TEST_P(QuicConnectionTest, BasicSending) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  const QuicConnectionStats& stats = connection_.GetStats();
  EXPECT_FALSE(stats.first_decrypted_packet.IsInitialized());
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacket(1);
  EXPECT_EQ(QuicPacketNumber(1), stats.first_decrypted_packet);
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 2);
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);  // Packet 1
  EXPECT_EQ(QuicPacketNumber(1u), last_packet);
  SendAckPacketToPeer();  // Packet 2

  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(1u), least_unacked());
  }

  SendAckPacketToPeer();  // Packet 3
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(1u), least_unacked());
  }

  SendStreamDataToPeer(1, "bar", 3, NO_FIN, &last_packet);  // Packet 4
  EXPECT_EQ(QuicPacketNumber(4u), last_packet);
  SendAckPacketToPeer();  // Packet 5
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(1u), least_unacked());
  }

  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));

  // Peer acks up to packet 3.
  QuicAckFrame frame = InitAckFrame(3);
  ProcessAckPacket(&frame);
  SendAckPacketToPeer();  // Packet 6

  // As soon as we've acked one, we skip ack packets 2 and 3 and note lack of
  // ack for 4.
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(4u), least_unacked());
  }

  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));

  // Peer acks up to packet 4, the last packet.
  QuicAckFrame frame2 = InitAckFrame(6);
  ProcessAckPacket(&frame2);  // Acks don't instigate acks.

  // Verify that we did not send an ack.
  EXPECT_EQ(QuicPacketNumber(6u), writer_->header().packet_number);

  // So the last ack has not changed.
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(4u), least_unacked());
  }

  // If we force an ack, we shouldn't change our retransmit state.
  SendAckPacketToPeer();  // Packet 7
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(7u), least_unacked());
  }

  // But if we send more data it should.
  SendStreamDataToPeer(1, "eep", 6, NO_FIN, &last_packet);  // Packet 8
  EXPECT_EQ(QuicPacketNumber(8u), last_packet);
  SendAckPacketToPeer();  // Packet 9
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(7u), least_unacked());
  }
  EXPECT_EQ(QuicPacketNumber(1), stats.first_decrypted_packet);
}

// QuicConnection should record the packet sent-time prior to sending the
// packet.
TEST_P(QuicConnectionTest, RecordSentTimeBeforePacketSent) {
  // We're using a MockClock for the tests, so we have complete control over the
  // time.
  // Our recorded timestamp for the last packet sent time will be passed in to
  // the send_algorithm.  Make sure that it is set to the correct value.
  QuicTime actual_recorded_send_time = QuicTime::Zero();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<0>(&actual_recorded_send_time));

  // First send without any pause and check the result.
  QuicTime expected_recorded_send_time = clock_.Now();
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_EQ(expected_recorded_send_time, actual_recorded_send_time)
      << "Expected time = " << expected_recorded_send_time.ToDebuggingValue()
      << ".  Actual time = " << actual_recorded_send_time.ToDebuggingValue();

  // Now pause during the write, and check the results.
  actual_recorded_send_time = QuicTime::Zero();
  const QuicTime::Delta write_pause_time_delta =
      QuicTime::Delta::FromMilliseconds(5000);
  SetWritePauseTimeDelta(write_pause_time_delta);
  expected_recorded_send_time = clock_.Now();

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<0>(&actual_recorded_send_time));
  connection_.SendStreamDataWithString(2, "baz", 0, NO_FIN);
  EXPECT_EQ(expected_recorded_send_time, actual_recorded_send_time)
      << "Expected time = " << expected_recorded_send_time.ToDebuggingValue()
      << ".  Actual time = " << actual_recorded_send_time.ToDebuggingValue();
}

TEST_P(QuicConnectionTest, FramePacking) {
  // Send two stream frames in 1 packet by queueing them.
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendStreamData3();
    connection_.SendStreamData5();
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  }
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's an ack and two stream frames from
  // two different streams.
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(2u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(2u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  }

  EXPECT_TRUE(writer_->ack_frames().empty());

  ASSERT_EQ(2u, writer_->stream_frames().size());
  EXPECT_EQ(GetNthClientInitiatedStreamId(1, connection_.transport_version()),
            writer_->stream_frames()[0]->stream_id);
  EXPECT_EQ(GetNthClientInitiatedStreamId(2, connection_.transport_version()),
            writer_->stream_frames()[1]->stream_id);
}

TEST_P(QuicConnectionTest, FramePackingNonCryptoThenCrypto) {
  // Send two stream frames (one non-crypto, then one crypto) in 2 packets by
  // queueing them.
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendStreamData3();
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
    connection_.SendCryptoStreamData();
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  }
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's the crypto stream frame.
  EXPECT_EQ(2u, writer_->frame_count());
  ASSERT_EQ(1u, writer_->padding_frames().size());
  if (!QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    ASSERT_EQ(1u, writer_->stream_frames().size());
    EXPECT_EQ(QuicUtils::GetCryptoStreamId(connection_.transport_version()),
              writer_->stream_frames()[0]->stream_id);
  } else {
    EXPECT_EQ(1u, writer_->crypto_frames().size());
  }
}

TEST_P(QuicConnectionTest, FramePackingCryptoThenNonCrypto) {
  // Send two stream frames (one crypto, then one non-crypto) in 2 packets by
  // queueing them.
  {
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendCryptoStreamData();
    connection_.SendStreamData3();
  }
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's the stream frame from stream 3.
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  ASSERT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(GetNthClientInitiatedStreamId(1, connection_.transport_version()),
            writer_->stream_frames()[0]->stream_id);
}

TEST_P(QuicConnectionTest, FramePackingAckResponse) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  // Process a data packet to queue up a pending ack.
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  }
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);

  QuicPacketNumber last_packet;
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    connection_.SendCryptoDataWithString("foo", 0);
  } else {
    SendStreamDataToPeer(
        QuicUtils::GetCryptoStreamId(connection_.transport_version()), "foo", 0,
        NO_FIN, &last_packet);
  }
  // Verify ack is bundled with outging packet.
  EXPECT_FALSE(writer_->ack_frames().empty());

  EXPECT_CALL(visitor_, OnCanWrite())
      .WillOnce(DoAll(IgnoreResult(InvokeWithoutArgs(
                          &connection_, &TestConnection::SendStreamData3)),
                      IgnoreResult(InvokeWithoutArgs(
                          &connection_, &TestConnection::SendStreamData5))));

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);

  // Process a data packet to cause the visitor's OnCanWrite to be invoked.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0x01));
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypter>(0x01));
  ProcessDataPacket(2);

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's an ack and two stream frames from
  // two different streams.
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(3u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(4u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(writer_->ack_frames().empty());
  ASSERT_EQ(2u, writer_->stream_frames().size());
  EXPECT_EQ(GetNthClientInitiatedStreamId(1, connection_.transport_version()),
            writer_->stream_frames()[0]->stream_id);
  EXPECT_EQ(GetNthClientInitiatedStreamId(2, connection_.transport_version()),
            writer_->stream_frames()[1]->stream_id);
}

TEST_P(QuicConnectionTest, FramePackingSendv) {
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Send data in 1 packet by writing multiple blocks in a single iovector
  // using writev.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));

  char data[] = "ABCDEF";
  struct iovec iov[2];
  iov[0].iov_base = data;
  iov[0].iov_len = 4;
  iov[1].iov_base = data + 4;
  iov[1].iov_len = 2;
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      connection_.transport_version(), Perspective::IS_CLIENT);
  connection_.SaveAndSendStreamData(stream_id, iov, 2, 6, 0, NO_FIN);

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure multiple iovector blocks have
  // been packed into a single stream frame from one stream.
  EXPECT_EQ(1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(0u, writer_->padding_frames().size());
  QuicStreamFrame* frame = writer_->stream_frames()[0].get();
  EXPECT_EQ(stream_id, frame->stream_id);
  EXPECT_EQ("ABCDEF",
            absl::string_view(frame->data_buffer, frame->data_length));
}

TEST_P(QuicConnectionTest, FramePackingSendvQueued) {
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Try to send two stream frames in 1 packet by using writev.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));

  BlockOnNextWrite();
  char data[] = "ABCDEF";
  struct iovec iov[2];
  iov[0].iov_base = data;
  iov[0].iov_len = 4;
  iov[1].iov_base = data + 4;
  iov[1].iov_len = 2;
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      connection_.transport_version(), Perspective::IS_CLIENT);
  connection_.SaveAndSendStreamData(stream_id, iov, 2, 6, 0, NO_FIN);

  EXPECT_EQ(1u, connection_.NumQueuedPackets());
  EXPECT_TRUE(connection_.HasQueuedData());

  // Unblock the writes and actually send.
  writer_->SetWritable();
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());

  // Parse the last packet and ensure it's one stream frame from one stream.
  EXPECT_EQ(1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(0u, writer_->padding_frames().size());
  EXPECT_EQ(stream_id, writer_->stream_frames()[0]->stream_id);
}

TEST_P(QuicConnectionTest, SendingZeroBytes) {
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Send a zero byte write with a fin using writev.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  QuicStreamId stream_id = QuicUtils::GetFirstBidirectionalStreamId(
      connection_.transport_version(), Perspective::IS_CLIENT);
  connection_.SaveAndSendStreamData(stream_id, nullptr, 0, 0, 0, FIN);

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Padding frames are added by v99 to ensure a minimum packet size.
  size_t extra_padding_frames = 0;
  if (GetParam().version.HasHeaderProtection()) {
    extra_padding_frames = 1;
  }

  // Parse the last packet and ensure it's one stream frame from one stream.
  EXPECT_EQ(1u + extra_padding_frames, writer_->frame_count());
  EXPECT_EQ(extra_padding_frames, writer_->padding_frames().size());
  ASSERT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(stream_id, writer_->stream_frames()[0]->stream_id);
  EXPECT_TRUE(writer_->stream_frames()[0]->fin);
}

TEST_P(QuicConnectionTest, LargeSendWithPendingAck) {
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  // Set the ack alarm by processing a ping frame.
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  // Processs a PING frame.
  ProcessFramePacket(QuicFrame(QuicPingFrame()));
  // Ensure that this has caused the ACK alarm to be set.
  EXPECT_TRUE(connection_.HasPendingAcks());

  // Send data and ensure the ack is bundled.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(8);
  size_t len = 10000;
  std::unique_ptr<char[]> data_array(new char[len]);
  memset(data_array.get(), '?', len);
  struct iovec iov;
  iov.iov_base = data_array.get();
  iov.iov_len = len;
  QuicConsumedData consumed = connection_.SaveAndSendStreamData(
      GetNthClientInitiatedStreamId(0, connection_.transport_version()), &iov,
      1, len, 0, FIN);
  EXPECT_EQ(len, consumed.bytes_consumed);
  EXPECT_TRUE(consumed.fin_consumed);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's one stream frame with a fin.
  EXPECT_EQ(1u, writer_->frame_count());
  ASSERT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(GetNthClientInitiatedStreamId(0, connection_.transport_version()),
            writer_->stream_frames()[0]->stream_id);
  EXPECT_TRUE(writer_->stream_frames()[0]->fin);
  // Ensure the ack alarm was cancelled when the ack was sent.
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, OnCanWrite) {
  // Visitor's OnCanWrite will send data, but will have more pending writes.
  EXPECT_CALL(visitor_, OnCanWrite())
      .WillOnce(DoAll(IgnoreResult(InvokeWithoutArgs(
                          &connection_, &TestConnection::SendStreamData3)),
                      IgnoreResult(InvokeWithoutArgs(
                          &connection_, &TestConnection::SendStreamData5))));
  {
    InSequence seq;
    EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillOnce(Return(true));
    EXPECT_CALL(visitor_, WillingAndAbleToWrite())
        .WillRepeatedly(Return(false));
  }

  EXPECT_CALL(*send_algorithm_, CanSend(_))
      .WillRepeatedly(testing::Return(true));

  connection_.OnCanWrite();

  // Parse the last packet and ensure it's the two stream frames from
  // two different streams.
  EXPECT_EQ(2u, writer_->frame_count());
  EXPECT_EQ(2u, writer_->stream_frames().size());
  EXPECT_EQ(GetNthClientInitiatedStreamId(1, connection_.transport_version()),
            writer_->stream_frames()[0]->stream_id);
  EXPECT_EQ(GetNthClientInitiatedStreamId(2, connection_.transport_version()),
            writer_->stream_frames()[1]->stream_id);
}

TEST_P(QuicConnectionTest, RetransmitOnNack) {
  QuicPacketNumber last_packet;
  QuicByteCount second_packet_size;
  SendStreamDataToPeer(3, "foo", 0, NO_FIN, &last_packet);  // Packet 1
  second_packet_size =
      SendStreamDataToPeer(3, "foos", 3, NO_FIN, &last_packet);  // Packet 2
  SendStreamDataToPeer(3, "fooos", 7, NO_FIN, &last_packet);     // Packet 3

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  // Don't lose a packet on an ack, and nothing is retransmitted.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame ack_one = InitAckFrame(1);
  ProcessAckPacket(&ack_one);

  // Lose a packet and ensure it triggers retransmission.
  QuicAckFrame nack_two = ConstructAckFrame(3, 2);
  LostPacketVector lost_packets;
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(2), kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_FALSE(QuicPacketCreatorPeer::SendVersionInPacket(creator_));
  ProcessAckPacket(&nack_two);
}

TEST_P(QuicConnectionTest, DoNotSendQueuedPacketForResetStream) {
  // Block the connection to queue the packet.
  BlockOnNextWrite();

  QuicStreamId stream_id = 2;
  connection_.SendStreamDataWithString(stream_id, "foo", 0, NO_FIN);

  // Now that there is a queued packet, reset the stream.
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 3);

  // Unblock the connection and verify that only the RST_STREAM is sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  writer_->SetWritable();
  connection_.OnCanWrite();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->rst_stream_frames().size());
}

TEST_P(QuicConnectionTest, SendQueuedPacketForQuicRstStreamNoError) {
  // Block the connection to queue the packet.
  BlockOnNextWrite();

  QuicStreamId stream_id = 2;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(stream_id, "foo", 0, NO_FIN);

  // Now that there is a queued packet, reset the stream.
  SendRstStream(stream_id, QUIC_STREAM_NO_ERROR, 3);

  // Unblock the connection and verify that the RST_STREAM is sent and the data
  // packet is sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  writer_->SetWritable();
  connection_.OnCanWrite();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->rst_stream_frames().size());
}

TEST_P(QuicConnectionTest, DoNotRetransmitForResetStreamOnNack) {
  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "foos", 3, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "fooos", 7, NO_FIN, &last_packet);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 12);

  // Lose a packet and ensure it does not trigger retransmission.
  QuicAckFrame nack_two = ConstructAckFrame(last_packet, last_packet - 1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessAckPacket(&nack_two);
}

TEST_P(QuicConnectionTest, RetransmitForQuicRstStreamNoErrorOnNack) {
  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "foos", 3, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "fooos", 7, NO_FIN, &last_packet);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  SendRstStream(stream_id, QUIC_STREAM_NO_ERROR, 12);

  // Lose a packet, ensure it triggers retransmission.
  QuicAckFrame nack_two = ConstructAckFrame(last_packet, last_packet - 1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  LostPacketVector lost_packets;
  lost_packets.push_back(LostPacket(last_packet - 1, kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  ProcessAckPacket(&nack_two);
}

TEST_P(QuicConnectionTest, DoNotRetransmitForResetStreamOnRTO) {
  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 3);

  // Fire the RTO and verify that the RST_STREAM is resent, not stream data.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  clock_.AdvanceTime(DefaultRetransmissionTime());
  connection_.GetRetransmissionAlarm()->Fire();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->rst_stream_frames().size());
  EXPECT_EQ(stream_id, writer_->rst_stream_frames().front().stream_id);
}

// Ensure that if the only data in flight is non-retransmittable, the
// retransmission alarm is not set.
TEST_P(QuicConnectionTest, CancelRetransmissionAlarmAfterResetStream) {
  QuicStreamId stream_id = 2;
  QuicPacketNumber last_data_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_data_packet);

  // Cancel the stream.
  const QuicPacketNumber rst_packet = last_data_packet + 1;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, rst_packet, _, _)).Times(1);
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 3);

  // Ack the RST_STREAM frame (since it's retransmittable), but not the data
  // packet, which is no longer retransmittable since the stream was cancelled.
  QuicAckFrame nack_stream_data =
      ConstructAckFrame(rst_packet, last_data_packet);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessAckPacket(&nack_stream_data);

  // Ensure that the data is still in flight, but the retransmission alarm is no
  // longer set.
  EXPECT_GT(manager_->GetBytesInFlight(), 0u);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, RetransmitForQuicRstStreamNoErrorOnRTO) {
  connection_.SetMaxTailLossProbes(0);

  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  SendRstStream(stream_id, QUIC_STREAM_NO_ERROR, 3);

  // Fire the RTO and verify that the RST_STREAM is resent, the stream data
  // is sent.
  const size_t num_retransmissions =
      connection_.SupportsMultiplePacketNumberSpaces() ? 1 : 2;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(num_retransmissions));
  clock_.AdvanceTime(DefaultRetransmissionTime());
  connection_.GetRetransmissionAlarm()->Fire();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  if (num_retransmissions == 2) {
    ASSERT_EQ(1u, writer_->rst_stream_frames().size());
    EXPECT_EQ(stream_id, writer_->rst_stream_frames().front().stream_id);
  }
}

TEST_P(QuicConnectionTest, DoNotSendPendingRetransmissionForResetStream) {
  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "foos", 3, NO_FIN, &last_packet);
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(stream_id, "fooos", 7, NO_FIN);

  // Lose a packet which will trigger a pending retransmission.
  QuicAckFrame ack = ConstructAckFrame(last_packet, last_packet - 1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessAckPacket(&ack);

  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 12);

  // Unblock the connection and verify that the RST_STREAM is sent but not the
  // second data packet nor a retransmit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  writer_->SetWritable();
  connection_.OnCanWrite();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  ASSERT_EQ(1u, writer_->rst_stream_frames().size());
  EXPECT_EQ(stream_id, writer_->rst_stream_frames().front().stream_id);
}

TEST_P(QuicConnectionTest, SendPendingRetransmissionForQuicRstStreamNoError) {
  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "foos", 3, NO_FIN, &last_packet);
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(stream_id, "fooos", 7, NO_FIN);

  // Lose a packet which will trigger a pending retransmission.
  QuicAckFrame ack = ConstructAckFrame(last_packet, last_packet - 1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  LostPacketVector lost_packets;
  lost_packets.push_back(LostPacket(last_packet - 1, kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessAckPacket(&ack);

  SendRstStream(stream_id, QUIC_STREAM_NO_ERROR, 12);

  // Unblock the connection and verify that the RST_STREAM is sent and the
  // second data packet or a retransmit is sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(2));
  writer_->SetWritable();
  connection_.OnCanWrite();
  // The RST_STREAM_FRAME is sent after queued packets and pending
  // retransmission.
  connection_.SendControlFrame(QuicFrame(
      new QuicRstStreamFrame(1, stream_id, QUIC_STREAM_NO_ERROR, 14)));
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->rst_stream_frames().size());
}

TEST_P(QuicConnectionTest, RetransmitAckedPacket) {
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);    // Packet 1
  SendStreamDataToPeer(1, "foos", 3, NO_FIN, &last_packet);   // Packet 2
  SendStreamDataToPeer(1, "fooos", 7, NO_FIN, &last_packet);  // Packet 3

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  // Instigate a loss with an ack.
  QuicAckFrame nack_two = ConstructAckFrame(3, 2);
  // The first nack should trigger a fast retransmission, but we'll be
  // write blocked, so the packet will be queued.
  BlockOnNextWrite();

  LostPacketVector lost_packets;
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(2), kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(4), _, _))
      .Times(1);
  ProcessAckPacket(&nack_two);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Now, ack the previous transmission.
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(false, _, _, _, _));
  QuicAckFrame ack_all = InitAckFrame(3);
  ProcessAckPacket(&ack_all);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(4), _, _))
      .Times(0);

  writer_->SetWritable();
  connection_.OnCanWrite();

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  // We do not store retransmittable frames of this retransmission.
  EXPECT_FALSE(QuicConnectionPeer::HasRetransmittableFrames(&connection_, 4));
}

TEST_P(QuicConnectionTest, RetransmitNackedLargestObserved) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  QuicPacketNumber original, second;

  QuicByteCount packet_size =
      SendStreamDataToPeer(3, "foo", 0, NO_FIN, &original);  // 1st packet.
  SendStreamDataToPeer(3, "bar", 3, NO_FIN, &second);        // 2nd packet.

  QuicAckFrame frame = InitAckFrame({{second, second + 1}});
  // The first nack should retransmit the largest observed packet.
  LostPacketVector lost_packets;
  lost_packets.push_back(LostPacket(original, kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  // Packet 1 is short header for IETF QUIC because the encryption level
  // switched to ENCRYPTION_FORWARD_SECURE in SendStreamDataToPeer.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _,
                           GetParam().version.HasIetfInvariantHeader()
                               ? packet_size
                               : packet_size - kQuicVersionSize,
                           _));
  ProcessAckPacket(&frame);
}

TEST_P(QuicConnectionTest, QueueAfterTwoRTOs) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(0);

  for (int i = 0; i < 10; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendStreamDataWithString(3, "foo", i * 3, NO_FIN);
  }

  // Block the writer and ensure they're queued.
  BlockOnNextWrite();
  clock_.AdvanceTime(DefaultRetransmissionTime());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_TRUE(connection_.HasQueuedData());

  // Unblock the writer.
  writer_->SetWritable();
  clock_.AdvanceTime(QuicTime::Delta::FromMicroseconds(
      2 * DefaultRetransmissionTime().ToMicroseconds()));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  connection_.GetRetransmissionAlarm()->Fire();
  connection_.OnCanWrite();
}

TEST_P(QuicConnectionTest, WriteBlockedBufferedThenSent) {
  BlockOnNextWrite();
  writer_->set_is_write_blocked_data_buffered(true);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  writer_->SetWritable();
  connection_.OnCanWrite();
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, WriteBlockedThenSent) {
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  BlockOnNextWrite();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // The second packet should also be queued, in order to ensure packets are
  // never sent out of order.
  writer_->SetWritable();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_EQ(2u, connection_.NumQueuedPackets());

  // Now both are sent in order when we unblock.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.OnCanWrite();
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_P(QuicConnectionTest, RetransmitWriteBlockedAckedOriginalThenSent) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  BlockOnNextWrite();
  writer_->set_is_write_blocked_data_buffered(true);
  // Simulate the retransmission alarm firing.
  clock_.AdvanceTime(DefaultRetransmissionTime());
  connection_.GetRetransmissionAlarm()->Fire();

  // Ack the sent packet before the callback returns, which happens in
  // rare circumstances with write blocked sockets.
  QuicAckFrame ack = InitAckFrame(1);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&ack);

  writer_->SetWritable();
  connection_.OnCanWrite();
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  uint64_t retransmission = connection_.SupportsMultiplePacketNumberSpaces() &&
                                    !GetQuicReloadableFlag(quic_default_on_pto)
                                ? 3
                                : 2;
  EXPECT_FALSE(QuicConnectionPeer::HasRetransmittableFrames(&connection_,
                                                            retransmission));
}

TEST_P(QuicConnectionTest, AlarmsWhenWriteBlocked) {
  // Block the connection.
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_TRUE(writer_->IsWriteBlocked());

  // Set the send alarm. Fire the alarm and ensure it doesn't attempt to write.
  connection_.GetSendAlarm()->Set(clock_.ApproximateNow());
  connection_.GetSendAlarm()->Fire();
  EXPECT_TRUE(writer_->IsWriteBlocked());
  EXPECT_EQ(1u, writer_->packets_write_attempts());
}

TEST_P(QuicConnectionTest, NoSendAlarmAfterProcessPacketWhenWriteBlocked) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  // Block the connection.
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  EXPECT_TRUE(writer_->IsWriteBlocked());
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  // Process packet number 1. Can not call ProcessPacket or ProcessDataPacket
  // here, because they will fire the alarm after QuicConnection::ProcessPacket
  // is returned.
  const uint64_t received_packet_num = 1;
  const bool has_stop_waiting = false;
  const EncryptionLevel level = ENCRYPTION_FORWARD_SECURE;
  std::unique_ptr<QuicPacket> packet(
      ConstructDataPacket(received_packet_num, has_stop_waiting, level));
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      peer_framer_.EncryptPayload(level, QuicPacketNumber(received_packet_num),
                                  *packet, buffer, kMaxOutgoingPacketSize);
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, encrypted_length, clock_.Now(), false));

  EXPECT_TRUE(writer_->IsWriteBlocked());
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, AddToWriteBlockedListIfWriterBlockedWhenProcessing) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, nullptr);

  // Simulate the case where a shared writer gets blocked by another connection.
  writer_->SetWriteBlocked();

  // Process an ACK, make sure the connection calls visitor_->OnWriteBlocked().
  QuicAckFrame ack1 = InitAckFrame(1);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(1);
  ProcessAckPacket(1, &ack1);
}

TEST_P(QuicConnectionTest, DoNotAddToWriteBlockedListAfterDisconnect) {
  writer_->SetBatchMode(true);
  EXPECT_TRUE(connection_.connected());
  // Have to explicitly grab the OnConnectionClosed frame and check
  // its parameters because this is a silent connection close and the
  // frame is not also transmitted to the peer.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));

  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(0);

  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.CloseConnection(QUIC_PEER_GOING_AWAY, "no reason",
                                ConnectionCloseBehavior::SILENT_CLOSE);

    EXPECT_FALSE(connection_.connected());
    writer_->SetWriteBlocked();
  }
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PEER_GOING_AWAY));
}

TEST_P(QuicConnectionTest, AddToWriteBlockedListIfBlockedOnFlushPackets) {
  writer_->SetBatchMode(true);
  writer_->BlockOnNextFlush();

  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(1);
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    // flusher's destructor will call connection_.FlushPackets, which should add
    // the connection to the write blocked list.
  }
}

TEST_P(QuicConnectionTest, NoLimitPacketsPerNack) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  int offset = 0;
  // Send packets 1 to 15.
  for (int i = 0; i < 15; ++i) {
    SendStreamDataToPeer(1, "foo", offset, NO_FIN, nullptr);
    offset += 3;
  }

  // Ack 15, nack 1-14.

  QuicAckFrame nack =
      InitAckFrame({{QuicPacketNumber(15), QuicPacketNumber(16)}});

  // 14 packets have been NACK'd and lost.
  LostPacketVector lost_packets;
  for (int i = 1; i < 15; ++i) {
    lost_packets.push_back(
        LostPacket(QuicPacketNumber(i), kMaxOutgoingPacketSize));
  }
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessAckPacket(&nack);
}

// Test sending multiple acks from the connection to the session.
TEST_P(QuicConnectionTest, MultipleAcks) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacket(1);
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 2);
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);  // Packet 1
  EXPECT_EQ(QuicPacketNumber(1u), last_packet);
  SendStreamDataToPeer(3, "foo", 0, NO_FIN, &last_packet);  // Packet 2
  EXPECT_EQ(QuicPacketNumber(2u), last_packet);
  SendAckPacketToPeer();                                    // Packet 3
  SendStreamDataToPeer(5, "foo", 0, NO_FIN, &last_packet);  // Packet 4
  EXPECT_EQ(QuicPacketNumber(4u), last_packet);
  SendStreamDataToPeer(1, "foo", 3, NO_FIN, &last_packet);  // Packet 5
  EXPECT_EQ(QuicPacketNumber(5u), last_packet);
  SendStreamDataToPeer(3, "foo", 3, NO_FIN, &last_packet);  // Packet 6
  EXPECT_EQ(QuicPacketNumber(6u), last_packet);

  // Client will ack packets 1, 2, [!3], 4, 5.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame1 = ConstructAckFrame(5, 3);
  ProcessAckPacket(&frame1);

  // Now the client implicitly acks 3, and explicitly acks 6.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame2 = InitAckFrame(6);
  ProcessAckPacket(&frame2);
}

TEST_P(QuicConnectionTest, DontLatchUnackedPacket) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacket(1);
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 2);
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, nullptr);  // Packet 1;
  // From now on, we send acks, so the send algorithm won't mark them pending.
  SendAckPacketToPeer();  // Packet 2

  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame = InitAckFrame(1);
  ProcessAckPacket(&frame);

  // Verify that our internal state has least-unacked as 2, because we're still
  // waiting for a potential ack for 2.

  EXPECT_EQ(QuicPacketNumber(2u), stop_waiting()->least_unacked);

  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  frame = InitAckFrame(2);
  ProcessAckPacket(&frame);
  EXPECT_EQ(QuicPacketNumber(3u), stop_waiting()->least_unacked);

  // When we send an ack, we make sure our least-unacked makes sense.  In this
  // case since we're not waiting on an ack for 2 and all packets are acked, we
  // set it to 3.
  SendAckPacketToPeer();  // Packet 3
  // Least_unacked remains at 3 until another ack is received.
  EXPECT_EQ(QuicPacketNumber(3u), stop_waiting()->least_unacked);
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    // Check that the outgoing ack had its packet number as least_unacked.
    EXPECT_EQ(QuicPacketNumber(3u), least_unacked());
  }

  // Ack the ack, which updates the rtt and raises the least unacked.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  frame = InitAckFrame(3);
  ProcessAckPacket(&frame);

  SendStreamDataToPeer(1, "bar", 3, NO_FIN, nullptr);  // Packet 4
  EXPECT_EQ(QuicPacketNumber(4u), stop_waiting()->least_unacked);
  SendAckPacketToPeer();  // Packet 5
  if (GetParam().no_stop_waiting) {
    // Expect no stop waiting frame is sent.
    EXPECT_FALSE(least_unacked().IsInitialized());
  } else {
    EXPECT_EQ(QuicPacketNumber(4u), least_unacked());
  }

  // Send two data packets at the end, and ensure if the last one is acked,
  // the least unacked is raised above the ack packets.
  SendStreamDataToPeer(1, "bar", 6, NO_FIN, nullptr);  // Packet 6
  SendStreamDataToPeer(1, "bar", 9, NO_FIN, nullptr);  // Packet 7

  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  frame = InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(5)},
                        {QuicPacketNumber(7), QuicPacketNumber(8)}});
  ProcessAckPacket(&frame);

  EXPECT_EQ(QuicPacketNumber(6u), stop_waiting()->least_unacked);
}

TEST_P(QuicConnectionTest, TLP) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(1);

  SendStreamDataToPeer(3, "foo", 0, NO_FIN, nullptr);
  EXPECT_EQ(QuicPacketNumber(1u), stop_waiting()->least_unacked);
  QuicTime retransmission_time =
      connection_.GetRetransmissionAlarm()->deadline();
  EXPECT_NE(QuicTime::Zero(), retransmission_time);

  EXPECT_EQ(QuicPacketNumber(1u), writer_->header().packet_number);
  // Simulate the retransmission alarm firing and sending a tlp,
  // so send algorithm's OnRetransmissionTimeout is not called.
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  const QuicPacketNumber retransmission(
      connection_.SupportsMultiplePacketNumberSpaces() ? 3 : 2);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, retransmission, _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(retransmission, writer_->header().packet_number);
  // We do not raise the high water mark yet.
  EXPECT_EQ(QuicPacketNumber(1u), stop_waiting()->least_unacked);
}

TEST_P(QuicConnectionTest, TailLossProbeDelayForStreamDataInTLPR) {
  if (connection_.PtoEnabled()) {
    return;
  }

  // Set TLPR from QuicConfig.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kTLPR);
  config.SetConnectionOptionsToSend(options);
  connection_.SetFromConfig(config);
  connection_.SetMaxTailLossProbes(1);

  SendStreamDataToPeer(3, "foo", 0, NO_FIN, nullptr);
  EXPECT_EQ(QuicPacketNumber(1u), stop_waiting()->least_unacked);

  QuicTime retransmission_time =
      connection_.GetRetransmissionAlarm()->deadline();
  EXPECT_NE(QuicTime::Zero(), retransmission_time);
  QuicTime::Delta expected_tlp_delay =
      0.5 * manager_->GetRttStats()->SmoothedOrInitialRtt();
  EXPECT_EQ(expected_tlp_delay, retransmission_time - clock_.Now());

  EXPECT_EQ(QuicPacketNumber(1u), writer_->header().packet_number);
  // Simulate firing of the retransmission alarm and retransmit the packet.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(2), _, _));
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(QuicPacketNumber(2u), writer_->header().packet_number);

  // We do not raise the high water mark yet.
  EXPECT_EQ(QuicPacketNumber(1u), stop_waiting()->least_unacked);
}

TEST_P(QuicConnectionTest, TailLossProbeDelayForNonStreamDataInTLPR) {
  if (connection_.PtoEnabled()) {
    return;
  }

  // Set TLPR from QuicConfig.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kTLPR);
  config.SetConnectionOptionsToSend(options);
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  connection_.SetFromConfig(config);
  connection_.SetMaxTailLossProbes(1);

  // Sets retransmittable on wire.
  const QuicTime::Delta retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(50);
  connection_.set_initial_retransmittable_on_wire_timeout(
      retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  // Send a data packet.
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;

  // Path degrading alarm should be set when there is a retransmittable packet
  // on the wire.
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());

  // Verify the path degrading delay.
  // First TLP with stream data.
  QuicTime::Delta srtt = manager_->GetRttStats()->SmoothedOrInitialRtt();
  QuicTime::Delta expected_delay = 0.5 * srtt;
  // Add 1st RTO.
  QuicTime::Delta retransmission_delay =
      QuicTime::Delta::FromMilliseconds(kDefaultRetransmissionTimeMs);
  expected_delay = expected_delay + retransmission_delay;
  // Add 2nd RTO.
  expected_delay = expected_delay + retransmission_delay * 2;
  EXPECT_EQ(expected_delay,
            QuicConnectionPeer::GetSentPacketManager(&connection_)
                ->GetPathDegradingDelay());
  ASSERT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());

  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Receive an ACK for the data packet.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(&frame);

  // Path degrading alarm should be cancelled as there is no more
  // reretransmittable packets on the wire.
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  // The ping alarm should be set to the retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Simulate firing of the retransmittable on wire and send a PING.
  clock_.AdvanceTime(retransmittable_on_wire_timeout);
  connection_.GetPingAlarm()->Fire();

  // The retransmission alarm and the path degrading alarm should be set as
  // there is a retransmittable packet (PING) on the wire,
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());

  // Verify the retransmission delay.
  QuicTime::Delta min_rto_timeout =
      QuicTime::Delta::FromMilliseconds(kMinRetransmissionTimeMs);
  srtt = manager_->GetRttStats()->SmoothedOrInitialRtt();

  // Arm RTO mode since there is only PING in flight.
  expected_delay = manager_->GetPtoDelay();
  EXPECT_EQ(expected_delay,
            connection_.GetRetransmissionAlarm()->deadline() - clock_.Now());

  // Verify the path degrading delay = TLP delay + 1st RTO + 2nd RTO.
  // Add 1st RTO.
  expected_delay = std::max(2 * srtt, 1.5 * srtt + 0.5 * min_rto_timeout);
  retransmission_delay =
      std::max(manager_->GetRttStats()->smoothed_rtt() +
                   4 * manager_->GetRttStats()->mean_deviation(),
               min_rto_timeout);
  expected_delay = expected_delay + retransmission_delay;
  // Add 2nd RTO.
  expected_delay = expected_delay + retransmission_delay * 2;
  EXPECT_EQ(expected_delay,
            QuicConnectionPeer::GetSentPacketManager(&connection_)
                ->GetPathDegradingDelay());

  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Advance a small period of time: 5ms. And receive a retransmitted ACK.
  // This will update the retransmission alarm, verify the retransmission delay
  // is correct.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicAckFrame ack = InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(&ack);

  // Verify the retransmission delay.
  // First TLP without unacked stream data will no longer use TLPR.
  // Arm RTO mode since there is only PING in flight.
  expected_delay = manager_->GetPtoDelay();
  expected_delay = expected_delay - QuicTime::Delta::FromMilliseconds(5);
  EXPECT_EQ(expected_delay,
            connection_.GetRetransmissionAlarm()->deadline() - clock_.Now());
}

TEST_P(QuicConnectionTest, RTO) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(0);

  QuicTime default_retransmission_time =
      clock_.ApproximateNow() + DefaultRetransmissionTime();
  SendStreamDataToPeer(3, "foo", 0, NO_FIN, nullptr);
  EXPECT_EQ(QuicPacketNumber(1u), stop_waiting()->least_unacked);

  EXPECT_EQ(QuicPacketNumber(1u), writer_->header().packet_number);
  EXPECT_EQ(default_retransmission_time,
            connection_.GetRetransmissionAlarm()->deadline());
  // Simulate the retransmission alarm firing.
  clock_.AdvanceTime(DefaultRetransmissionTime());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(2), _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(QuicPacketNumber(2u), writer_->header().packet_number);
  // We do not raise the high water mark yet.
  EXPECT_EQ(QuicPacketNumber(1u), stop_waiting()->least_unacked);
}

// Regression test of b/133771183.
TEST_P(QuicConnectionTest, RtoWithNoDataToRetransmit) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  connection_.SetMaxTailLossProbes(0);

  SendStreamDataToPeer(3, "foo", 0, NO_FIN, nullptr);
  // Connection is cwnd limited.
  CongestionBlockWrites();
  // Stream gets reset.
  SendRstStream(3, QUIC_ERROR_PROCESSING_STREAM, 3);
  // Simulate the retransmission alarm firing.
  clock_.AdvanceTime(DefaultRetransmissionTime());
  // RTO fires, but there is no packet to be RTOed.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, writer_->rst_stream_frames().size());

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(40);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(20);
  EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillRepeatedly(Return(false));
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame()).Times(1);
  // Receives packets 1 - 40.
  for (size_t i = 1; i <= 40; ++i) {
    ProcessDataPacket(i);
  }
}

TEST_P(QuicConnectionTest, SendHandshakeMessages) {
  use_tagging_decrypter();
  // A TaggingEncrypter puts kTagSize copies of the given byte (0x01 here) at
  // the end of the packet. We can test this to check which encrypter was used.
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));

  // Attempt to send a handshake message and have the socket block.
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  BlockOnNextWrite();
  connection_.SendCryptoDataWithString("foo", 0);
  // The packet should be serialized, but not queued.
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Switch to the new encrypter.
  connection_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);

  // Now become writeable and flush the packets.
  writer_->SetWritable();
  EXPECT_CALL(visitor_, OnCanWrite());
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());

  // Verify that the handshake packet went out at the null encryption.
  EXPECT_EQ(0x01010101u, writer_->final_bytes_of_last_packet());
}

TEST_P(QuicConnectionTest,
       DropRetransmitsForNullEncryptedPacketAfterForwardSecure) {
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SendCryptoStreamData();

  // Simulate the retransmission alarm firing and the socket blocking.
  BlockOnNextWrite();
  clock_.AdvanceTime(DefaultRetransmissionTime());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Go forward secure.
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  notifier_.NeuterUnencryptedData();
  connection_.NeuterUnencryptedPackets();
  connection_.OnHandshakeComplete();

  EXPECT_EQ(QuicTime::Zero(), connection_.GetRetransmissionAlarm()->deadline());
  // Unblock the socket and ensure that no packets are sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  writer_->SetWritable();
  connection_.OnCanWrite();
}

TEST_P(QuicConnectionTest, RetransmitPacketsWithInitialEncryption) {
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);

  connection_.SendCryptoDataWithString("foo", 0);

  connection_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);

  SendStreamDataToPeer(2, "bar", 0, NO_FIN, nullptr);
  EXPECT_FALSE(notifier_.HasLostStreamData());
  connection_.MarkZeroRttPacketsForRetransmission(0);
  EXPECT_TRUE(notifier_.HasLostStreamData());
}

TEST_P(QuicConnectionTest, BufferNonDecryptablePackets) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  // SetFromConfig is always called after construction from InitializeSession.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  use_tagging_decrypter();

  const uint8_t tag = 0x07;
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(tag));

  // Process an encrypted packet which can not yet be decrypted which should
  // result in the packet being buffered.
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);

  // Transition to the new encryption state and process another encrypted packet
  // which should result in the original packet being processed.
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(tag));
  connection_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                           std::make_unique<TaggingEncrypter>(tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(2);
  ProcessDataPacketAtLevel(2, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);

  // Finally, process a third packet and note that we do not reprocess the
  // buffered packet.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(3, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
}

TEST_P(QuicConnectionTest, TestRetransmitOrder) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(0);

  QuicByteCount first_packet_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&first_packet_size));

  connection_.SendStreamDataWithString(3, "first_packet", 0, NO_FIN);
  QuicByteCount second_packet_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&second_packet_size));
  connection_.SendStreamDataWithString(3, "second_packet", 12, NO_FIN);
  EXPECT_NE(first_packet_size, second_packet_size);
  // Advance the clock by huge time to make sure packets will be retransmitted.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(10));
  {
    InSequence s;
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, first_packet_size, _));
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, second_packet_size, _));
  }
  connection_.GetRetransmissionAlarm()->Fire();

  // Advance again and expect the packets to be sent again in the same order.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(20));
  {
    InSequence s;
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, first_packet_size, _));
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, second_packet_size, _));
  }
  connection_.GetRetransmissionAlarm()->Fire();
}

TEST_P(QuicConnectionTest, Buffer100NonDecryptablePacketsThenKeyChange) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  // SetFromConfig is always called after construction from InitializeSession.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  config.set_max_undecryptable_packets(100);
  connection_.SetFromConfig(config);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  use_tagging_decrypter();

  const uint8_t tag = 0x07;
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(tag));

  // Process an encrypted packet which can not yet be decrypted which should
  // result in the packet being buffered.
  for (uint64_t i = 1; i <= 100; ++i) {
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  }

  // Transition to the new encryption state and process another encrypted packet
  // which should result in the original packets being processed.
  EXPECT_FALSE(connection_.GetProcessUndecryptablePacketsAlarm()->IsSet());
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(tag));
  EXPECT_TRUE(connection_.GetProcessUndecryptablePacketsAlarm()->IsSet());
  connection_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                           std::make_unique<TaggingEncrypter>(tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(100);
  connection_.GetProcessUndecryptablePacketsAlarm()->Fire();

  // Finally, process a third packet and note that we do not reprocess the
  // buffered packet.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(102, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
}

TEST_P(QuicConnectionTest, SetRTOAfterWritingToSocket) {
  BlockOnNextWrite();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Test that RTO is started once we write to the socket.
  writer_->SetWritable();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.OnCanWrite();
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, DelayRTOWithAckReceipt) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(0);

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  connection_.SendStreamDataWithString(2, "foo", 0, NO_FIN);
  connection_.SendStreamDataWithString(3, "bar", 0, NO_FIN);
  QuicAlarm* retransmission_alarm = connection_.GetRetransmissionAlarm();
  EXPECT_TRUE(retransmission_alarm->IsSet());
  EXPECT_EQ(DefaultRetransmissionTime(),
            retransmission_alarm->deadline() - clock_.Now());

  // Advance the time right before the RTO, then receive an ack for the first
  // packet to delay the RTO.
  clock_.AdvanceTime(DefaultRetransmissionTime());
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame ack = InitAckFrame(1);
  ProcessAckPacket(&ack);
  // Now we have an RTT sample of DefaultRetransmissionTime(500ms),
  // so the RTO has increased to 2 * SRTT.
  EXPECT_TRUE(retransmission_alarm->IsSet());
  EXPECT_EQ(retransmission_alarm->deadline() - clock_.Now(),
            2 * DefaultRetransmissionTime());

  // Move forward past the original RTO and ensure the RTO is still pending.
  clock_.AdvanceTime(2 * DefaultRetransmissionTime());

  // Ensure the second packet gets retransmitted when it finally fires.
  EXPECT_TRUE(retransmission_alarm->IsSet());
  EXPECT_EQ(retransmission_alarm->deadline(), clock_.ApproximateNow());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  // Manually cancel the alarm to simulate a real test.
  connection_.GetRetransmissionAlarm()->Fire();

  // The new retransmitted packet number should set the RTO to a larger value
  // than previously.
  EXPECT_TRUE(retransmission_alarm->IsSet());
  QuicTime next_rto_time = retransmission_alarm->deadline();
  QuicTime expected_rto_time =
      connection_.sent_packet_manager().GetRetransmissionTime();
  EXPECT_EQ(next_rto_time, expected_rto_time);
}

TEST_P(QuicConnectionTest, TestQueued) {
  connection_.SetMaxTailLossProbes(0);

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Unblock the writes and actually send.
  writer_->SetWritable();
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_P(QuicConnectionTest, InitialTimeout) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());

  // SetFromConfig sets the initial timeouts before negotiation.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);
  // Subtract a second from the idle timeout on the client side.
  QuicTime default_timeout =
      clock_.ApproximateNow() +
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  EXPECT_EQ(default_timeout, connection_.GetTimeoutAlarm()->deadline());

  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  // Simulate the timeout alarm firing.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1));
  connection_.GetTimeoutAlarm()->Fire();

  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());

  EXPECT_FALSE(connection_.HasPendingAcks());
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetProcessUndecryptablePacketsAlarm()->IsSet());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, IdleTimeoutAfterFirstSentPacket) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());

  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  QuicTime initial_ddl =
      clock_.ApproximateNow() +
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  EXPECT_EQ(initial_ddl, connection_.GetTimeoutAlarm()->deadline());
  EXPECT_TRUE(connection_.connected());

  // Advance the time and send the first packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(20));
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(1u), last_packet);
  // This will be the updated deadline for the connection to idle time out.
  QuicTime new_ddl = clock_.ApproximateNow() +
                     QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);

  // Simulate the timeout alarm firing, the connection should not be closed as
  // a new packet has been sent.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);
  QuicTime::Delta delay = initial_ddl - clock_.ApproximateNow();
  clock_.AdvanceTime(delay);
  // Verify the timeout alarm deadline is updated.
  EXPECT_TRUE(connection_.connected());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_EQ(new_ddl, connection_.GetTimeoutAlarm()->deadline());

  // Simulate the timeout alarm firing again, the connection now should be
  // closed.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  clock_.AdvanceTime(new_ddl - clock_.ApproximateNow());
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());

  EXPECT_FALSE(connection_.HasPendingAcks());
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, IdleTimeoutAfterSendTwoPackets) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());

  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  QuicTime initial_ddl =
      clock_.ApproximateNow() +
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  EXPECT_EQ(initial_ddl, connection_.GetTimeoutAlarm()->deadline());
  EXPECT_TRUE(connection_.connected());

  // Immediately send the first packet, this is a rare case but test code will
  // hit this issue often as MockClock used for tests doesn't move with code
  // execution until manually adjusted.
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(1u), last_packet);

  // Advance the time and send the second packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(20));
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(2u), last_packet);

  // Simulate the timeout alarm firing, the connection will be closed.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  clock_.AdvanceTime(initial_ddl - clock_.ApproximateNow());
  connection_.GetTimeoutAlarm()->Fire();

  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());

  EXPECT_FALSE(connection_.HasPendingAcks());
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, HandshakeTimeout) {
  // Use a shorter handshake timeout than idle timeout for this test.
  const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(5);
  connection_.SetNetworkTimeouts(timeout, timeout);
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());

  QuicTime handshake_timeout =
      clock_.ApproximateNow() + timeout - QuicTime::Delta::FromSeconds(1);
  EXPECT_EQ(handshake_timeout, connection_.GetTimeoutAlarm()->deadline());
  EXPECT_TRUE(connection_.connected());

  // Send and ack new data 3 seconds later to lengthen the idle timeout.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(0, connection_.transport_version()),
      "GET /", 0, FIN, nullptr);
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(3));
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&frame);

  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_TRUE(connection_.connected());

  clock_.AdvanceTime(timeout - QuicTime::Delta::FromSeconds(2));

  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  // Simulate the timeout alarm firing.
  connection_.GetTimeoutAlarm()->Fire();

  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());

  EXPECT_FALSE(connection_.HasPendingAcks());
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());
  TestConnectionCloseQuicErrorCode(QUIC_HANDSHAKE_TIMEOUT);
}

TEST_P(QuicConnectionTest, PingAfterSend) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());

  // Advance to 5ms, and send a packet to the peer, which will set
  // the ping alarm.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(0, connection_.transport_version()),
      "GET /", 0, FIN, nullptr);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(15),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now recevie an ACK of the previous packet, which will move the
  // ping alarm forward.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  // The ping timer is set slightly less than 15 seconds in the future, because
  // of the 1s ping timer alarm granularity.
  EXPECT_EQ(
      QuicTime::Delta::FromSeconds(15) - QuicTime::Delta::FromMilliseconds(5),
      connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  writer_->Reset();
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(15));
  connection_.GetPingAlarm()->Fire();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  ASSERT_EQ(1u, writer_->ping_frames().size());
  writer_->Reset();

  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(false));
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  SendAckPacketToPeer();

  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, ReducedPingTimeout) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());

  // Use a reduced ping timeout for this connection.
  connection_.set_ping_timeout(QuicTime::Delta::FromSeconds(10));

  // Advance to 5ms, and send a packet to the peer, which will set
  // the ping alarm.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(0, connection_.transport_version()),
      "GET /", 0, FIN, nullptr);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(10),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now recevie an ACK of the previous packet, which will move the
  // ping alarm forward.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  // The ping timer is set slightly less than 10 seconds in the future, because
  // of the 1s ping timer alarm granularity.
  EXPECT_EQ(
      QuicTime::Delta::FromSeconds(10) - QuicTime::Delta::FromMilliseconds(5),
      connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  writer_->Reset();
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(10));
  connection_.GetPingAlarm()->Fire();
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  ASSERT_EQ(1u, writer_->ping_frames().size());
  writer_->Reset();

  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(false));
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  SendAckPacketToPeer();

  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
}

// Tests whether sending an MTU discovery packet to peer successfully causes the
// maximum packet size to increase.
TEST_P(QuicConnectionTest, SendMtuDiscoveryPacket) {
  MtuDiscoveryTestInit();

  // Send an MTU probe.
  const size_t new_mtu = kDefaultMaxPacketSize + 100;
  QuicByteCount mtu_probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&mtu_probe_size));
  connection_.SendMtuDiscoveryPacket(new_mtu);
  EXPECT_EQ(new_mtu, mtu_probe_size);
  EXPECT_EQ(QuicPacketNumber(1u), creator_->packet_number());

  // Send more than MTU worth of data.  No acknowledgement was received so far,
  // so the MTU should be at its old value.
  const std::string data(kDefaultMaxPacketSize + 1, '.');
  QuicByteCount size_before_mtu_change;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(2)
      .WillOnce(SaveArg<3>(&size_before_mtu_change))
      .WillOnce(Return());
  connection_.SendStreamDataWithString(3, data, 0, FIN);
  EXPECT_EQ(QuicPacketNumber(3u), creator_->packet_number());
  EXPECT_EQ(kDefaultMaxPacketSize, size_before_mtu_change);

  // Acknowledge all packets so far.
  QuicAckFrame probe_ack = InitAckFrame(3);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&probe_ack);
  EXPECT_EQ(new_mtu, connection_.max_packet_length());

  // Send the same data again.  Check that it fits into a single packet now.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(3, data, 0, FIN);
  EXPECT_EQ(QuicPacketNumber(4u), creator_->packet_number());
}

// Verifies that when a MTU probe packet is sent and buffered in a batch writer,
// the writer is flushed immediately.
TEST_P(QuicConnectionTest, BatchWriterFlushedAfterMtuDiscoveryPacket) {
  writer_->SetBatchMode(true);
  MtuDiscoveryTestInit();

  // Send an MTU probe.
  const size_t target_mtu = kDefaultMaxPacketSize + 100;
  QuicByteCount mtu_probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&mtu_probe_size));
  const uint32_t prior_flush_attempts = writer_->flush_attempts();
  connection_.SendMtuDiscoveryPacket(target_mtu);
  EXPECT_EQ(target_mtu, mtu_probe_size);
  EXPECT_EQ(writer_->flush_attempts(), prior_flush_attempts + 1);
}

// Tests whether MTU discovery does not happen when it is not explicitly enabled
// by the connection options.
TEST_P(QuicConnectionTest, MtuDiscoveryDisabled) {
  MtuDiscoveryTestInit();

  const QuicPacketCount packets_between_probes_base = 10;
  set_packets_between_probes_base(packets_between_probes_base);

  const QuicPacketCount number_of_packets = packets_between_probes_base * 2;
  for (QuicPacketCount i = 0; i < number_of_packets; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    EXPECT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
    EXPECT_EQ(0u, connection_.mtu_probe_count());
  }
}

// Tests whether MTU discovery works when all probes are acknowledged on the
// first try.
TEST_P(QuicConnectionTest, MtuDiscoveryEnabled) {
  MtuDiscoveryTestInit();

  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  // Send enough packets so that the next one triggers path MTU discovery.
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the probe.
  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  QuicByteCount probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&probe_size));
  connection_.GetMtuDiscoveryAlarm()->Fire();

  EXPECT_THAT(probe_size, InRange(connection_.max_packet_length(),
                                  kMtuDiscoveryTargetPacketSizeHigh));

  const QuicPacketNumber probe_packet_number =
      FirstSendingPacketNumber() + packets_between_probes_base;
  ASSERT_EQ(probe_packet_number, creator_->packet_number());

  // Acknowledge all packets sent so far.
  QuicAckFrame probe_ack = InitAckFrame(probe_packet_number);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());
  ProcessAckPacket(&probe_ack);
  EXPECT_EQ(probe_size, connection_.max_packet_length());
  EXPECT_EQ(0u, connection_.GetBytesInFlight());

  EXPECT_EQ(1u, connection_.mtu_probe_count());

  QuicStreamOffset stream_offset = packets_between_probes_base;
  QuicByteCount last_probe_size = 0;
  for (size_t num_probes = 1; num_probes < kMtuDiscoveryAttempts;
       ++num_probes) {
    // Send just enough packets without triggering the next probe.
    for (QuicPacketCount i = 0;
         i < (packets_between_probes_base << num_probes) - 1; ++i) {
      SendStreamDataToPeer(3, ".", stream_offset++, NO_FIN, nullptr);
      ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
    }

    // Trigger the next probe.
    SendStreamDataToPeer(3, "!", stream_offset++, NO_FIN, nullptr);
    ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
    QuicByteCount new_probe_size;
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .WillOnce(SaveArg<3>(&new_probe_size));
    connection_.GetMtuDiscoveryAlarm()->Fire();
    EXPECT_THAT(new_probe_size,
                InRange(probe_size, kMtuDiscoveryTargetPacketSizeHigh));
    EXPECT_EQ(num_probes + 1, connection_.mtu_probe_count());

    // Acknowledge all packets sent so far.
    QuicAckFrame probe_ack = InitAckFrame(creator_->packet_number());
    ProcessAckPacket(&probe_ack);
    EXPECT_EQ(new_probe_size, connection_.max_packet_length());
    EXPECT_EQ(0u, connection_.GetBytesInFlight());

    last_probe_size = probe_size;
    probe_size = new_probe_size;
  }

  // The last probe size should be equal to the target.
  EXPECT_EQ(probe_size, kMtuDiscoveryTargetPacketSizeHigh);

  writer_->SetShouldWriteFail();

  // Ignore PACKET_WRITE_ERROR once.
  SendStreamDataToPeer(3, "(", stream_offset++, NO_FIN, nullptr);
  EXPECT_EQ(last_probe_size, connection_.max_packet_length());
  EXPECT_TRUE(connection_.connected());

  // Close connection on another PACKET_WRITE_ERROR.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  SendStreamDataToPeer(3, ")", stream_offset++, NO_FIN, nullptr);
  EXPECT_EQ(last_probe_size, connection_.max_packet_length());
  EXPECT_FALSE(connection_.connected());
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PACKET_WRITE_ERROR));
}

// After a successful MTU probe, one and only one write error should be ignored
// if it happened in QuicConnection::FlushPacket.
TEST_P(QuicConnectionTest,
       MtuDiscoveryIgnoreOneWriteErrorInFlushAfterSuccessfulProbes) {
  MtuDiscoveryTestInit();
  writer_->SetBatchMode(true);

  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  const QuicByteCount original_max_packet_length =
      connection_.max_packet_length();
  // Send enough packets so that the next one triggers path MTU discovery.
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the probe.
  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  QuicByteCount probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&probe_size));
  connection_.GetMtuDiscoveryAlarm()->Fire();

  EXPECT_THAT(probe_size, InRange(connection_.max_packet_length(),
                                  kMtuDiscoveryTargetPacketSizeHigh));

  const QuicPacketNumber probe_packet_number =
      FirstSendingPacketNumber() + packets_between_probes_base;
  ASSERT_EQ(probe_packet_number, creator_->packet_number());

  // Acknowledge all packets sent so far.
  QuicAckFrame probe_ack = InitAckFrame(probe_packet_number);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());
  ProcessAckPacket(&probe_ack);
  EXPECT_EQ(probe_size, connection_.max_packet_length());
  EXPECT_EQ(0u, connection_.GetBytesInFlight());

  EXPECT_EQ(1u, connection_.mtu_probe_count());

  writer_->SetShouldWriteFail();

  // Ignore PACKET_WRITE_ERROR once.
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    // flusher's destructor will call connection_.FlushPackets, which should
    // get a WRITE_STATUS_ERROR from the writer and ignore it.
  }
  EXPECT_EQ(original_max_packet_length, connection_.max_packet_length());
  EXPECT_TRUE(connection_.connected());

  // Close connection on another PACKET_WRITE_ERROR.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    // flusher's destructor will call connection_.FlushPackets, which should
    // get a WRITE_STATUS_ERROR from the writer and ignore it.
  }
  EXPECT_EQ(original_max_packet_length, connection_.max_packet_length());
  EXPECT_FALSE(connection_.connected());
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PACKET_WRITE_ERROR));
}

// Simulate the case where the first attempt to send a probe is write blocked,
// and after unblock, the second attempt returns a MSG_TOO_BIG error.
TEST_P(QuicConnectionTest, MtuDiscoveryWriteBlocked) {
  MtuDiscoveryTestInit();

  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  // Send enough packets so that the next one triggers path MTU discovery.
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  QuicByteCount original_max_packet_length = connection_.max_packet_length();

  // Trigger the probe.
  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  BlockOnNextWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  connection_.GetMtuDiscoveryAlarm()->Fire();
  EXPECT_EQ(1u, connection_.mtu_probe_count());
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
  ASSERT_TRUE(connection_.connected());

  writer_->SetWritable();
  SimulateNextPacketTooLarge();
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_EQ(original_max_packet_length, connection_.max_packet_length());
  EXPECT_TRUE(connection_.connected());
}

// Tests whether MTU discovery works correctly when the probes never get
// acknowledged.
TEST_P(QuicConnectionTest, MtuDiscoveryFailed) {
  MtuDiscoveryTestInit();

  // Lower the number of probes between packets in order to make the test go
  // much faster.
  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  const QuicTime::Delta rtt = QuicTime::Delta::FromMilliseconds(100);

  EXPECT_EQ(packets_between_probes_base,
            QuicConnectionPeer::GetPacketsBetweenMtuProbes(&connection_));

  // This tests sends more packets than strictly necessary to make sure that if
  // the connection was to send more discovery packets than needed, those would
  // get caught as well.
  const QuicPacketCount number_of_packets =
      packets_between_probes_base * (1 << (kMtuDiscoveryAttempts + 1));
  std::vector<QuicPacketNumber> mtu_discovery_packets;
  // Called on many acks.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());
  for (QuicPacketCount i = 0; i < number_of_packets; i++) {
    SendStreamDataToPeer(3, "!", i, NO_FIN, nullptr);
    clock_.AdvanceTime(rtt);

    // Receive an ACK, which marks all data packets as received, and all MTU
    // discovery packets as missing.

    QuicAckFrame ack;

    if (!mtu_discovery_packets.empty()) {
      QuicPacketNumber min_packet = *min_element(mtu_discovery_packets.begin(),
                                                 mtu_discovery_packets.end());
      QuicPacketNumber max_packet = *max_element(mtu_discovery_packets.begin(),
                                                 mtu_discovery_packets.end());
      ack.packets.AddRange(QuicPacketNumber(1), min_packet);
      ack.packets.AddRange(QuicPacketNumber(max_packet + 1),
                           creator_->packet_number() + 1);
      ack.largest_acked = creator_->packet_number();

    } else {
      ack.packets.AddRange(QuicPacketNumber(1), creator_->packet_number() + 1);
      ack.largest_acked = creator_->packet_number();
    }

    ProcessAckPacket(&ack);

    // Trigger MTU probe if it would be scheduled now.
    if (!connection_.GetMtuDiscoveryAlarm()->IsSet()) {
      continue;
    }

    // Fire the alarm.  The alarm should cause a packet to be sent.
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    connection_.GetMtuDiscoveryAlarm()->Fire();
    // Record the packet number of the MTU discovery packet in order to
    // mark it as NACK'd.
    mtu_discovery_packets.push_back(creator_->packet_number());
  }

  // Ensure the number of packets between probes grows exponentially by checking
  // it against the closed-form expression for the packet number.
  ASSERT_EQ(kMtuDiscoveryAttempts, mtu_discovery_packets.size());
  for (uint64_t i = 0; i < kMtuDiscoveryAttempts; i++) {
    // 2^0 + 2^1 + 2^2 + ... + 2^n = 2^(n + 1) - 1
    const QuicPacketCount packets_between_probes =
        packets_between_probes_base * ((1 << (i + 1)) - 1);
    EXPECT_EQ(QuicPacketNumber(packets_between_probes + (i + 1)),
              mtu_discovery_packets[i]);
  }

  EXPECT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  EXPECT_EQ(kDefaultMaxPacketSize, connection_.max_packet_length());
  EXPECT_EQ(kMtuDiscoveryAttempts, connection_.mtu_probe_count());
}

// Probe 3 times, the first one succeeds, then fails, then succeeds again.
TEST_P(QuicConnectionTest, MtuDiscoverySecondProbeFailed) {
  MtuDiscoveryTestInit();

  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  // Send enough packets so that the next one triggers path MTU discovery.
  QuicStreamOffset stream_offset = 0;
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", stream_offset++, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the probe.
  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  QuicByteCount probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&probe_size));
  connection_.GetMtuDiscoveryAlarm()->Fire();
  EXPECT_THAT(probe_size, InRange(connection_.max_packet_length(),
                                  kMtuDiscoveryTargetPacketSizeHigh));

  const QuicPacketNumber probe_packet_number =
      FirstSendingPacketNumber() + packets_between_probes_base;
  ASSERT_EQ(probe_packet_number, creator_->packet_number());

  // Acknowledge all packets sent so far.
  QuicAckFrame first_ack = InitAckFrame(probe_packet_number);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());
  ProcessAckPacket(&first_ack);
  EXPECT_EQ(probe_size, connection_.max_packet_length());
  EXPECT_EQ(0u, connection_.GetBytesInFlight());

  EXPECT_EQ(1u, connection_.mtu_probe_count());

  // Send just enough packets without triggering the second probe.
  for (QuicPacketCount i = 0; i < (packets_between_probes_base << 1) - 1; ++i) {
    SendStreamDataToPeer(3, ".", stream_offset++, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the second probe.
  SendStreamDataToPeer(3, "!", stream_offset++, NO_FIN, nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  QuicByteCount second_probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&second_probe_size));
  connection_.GetMtuDiscoveryAlarm()->Fire();
  EXPECT_THAT(second_probe_size,
              InRange(probe_size, kMtuDiscoveryTargetPacketSizeHigh));
  EXPECT_EQ(2u, connection_.mtu_probe_count());

  // Acknowledge all packets sent so far, except the second probe.
  QuicPacketNumber second_probe_packet_number = creator_->packet_number();
  QuicAckFrame second_ack = InitAckFrame(second_probe_packet_number - 1);
  ProcessAckPacket(&first_ack);
  EXPECT_EQ(probe_size, connection_.max_packet_length());

  // Send just enough packets without triggering the third probe.
  for (QuicPacketCount i = 0; i < (packets_between_probes_base << 2) - 1; ++i) {
    SendStreamDataToPeer(3, "@", stream_offset++, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the third probe.
  SendStreamDataToPeer(3, "#", stream_offset++, NO_FIN, nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  QuicByteCount third_probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&third_probe_size));
  connection_.GetMtuDiscoveryAlarm()->Fire();
  EXPECT_THAT(third_probe_size, InRange(probe_size, second_probe_size));
  EXPECT_EQ(3u, connection_.mtu_probe_count());

  // Acknowledge all packets sent so far, except the second probe.
  QuicAckFrame third_ack =
      ConstructAckFrame(creator_->packet_number(), second_probe_packet_number);
  ProcessAckPacket(&third_ack);
  EXPECT_EQ(third_probe_size, connection_.max_packet_length());

  SendStreamDataToPeer(3, "$", stream_offset++, NO_FIN, nullptr);
  EXPECT_TRUE(connection_.PathMtuReductionDetectionInProgress());

  if (connection_.PathDegradingDetectionInProgress() &&
      QuicConnectionPeer::GetPathDegradingDeadline(&connection_) <
          QuicConnectionPeer::GetPathMtuReductionDetectionDeadline(
              &connection_)) {
    // Fire path degrading alarm first.
    connection_.PathDegradingTimeout();
  }

  // Verify the max packet size has not reduced.
  EXPECT_EQ(third_probe_size, connection_.max_packet_length());

  // Fire alarm to get path mtu reduction callback called.
  EXPECT_TRUE(connection_.PathMtuReductionDetectionInProgress());
  connection_.GetBlackholeDetectorAlarm()->Fire();

  // Verify the max packet size has reduced to the previous value.
  EXPECT_EQ(probe_size, connection_.max_packet_length());
}

// Tests whether MTU discovery works when the writer has a limit on how large a
// packet can be.
TEST_P(QuicConnectionTest, MtuDiscoveryWriterLimited) {
  MtuDiscoveryTestInit();

  const QuicByteCount mtu_limit = kMtuDiscoveryTargetPacketSizeHigh - 1;
  writer_->set_max_packet_size(mtu_limit);

  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  // Send enough packets so that the next one triggers path MTU discovery.
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the probe.
  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  QuicByteCount probe_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(SaveArg<3>(&probe_size));
  connection_.GetMtuDiscoveryAlarm()->Fire();

  EXPECT_THAT(probe_size, InRange(connection_.max_packet_length(), mtu_limit));

  const QuicPacketNumber probe_sequence_number =
      FirstSendingPacketNumber() + packets_between_probes_base;
  ASSERT_EQ(probe_sequence_number, creator_->packet_number());

  // Acknowledge all packets sent so far.
  QuicAckFrame probe_ack = InitAckFrame(probe_sequence_number);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());
  ProcessAckPacket(&probe_ack);
  EXPECT_EQ(probe_size, connection_.max_packet_length());
  EXPECT_EQ(0u, connection_.GetBytesInFlight());

  EXPECT_EQ(1u, connection_.mtu_probe_count());

  QuicStreamOffset stream_offset = packets_between_probes_base;
  for (size_t num_probes = 1; num_probes < kMtuDiscoveryAttempts;
       ++num_probes) {
    // Send just enough packets without triggering the next probe.
    for (QuicPacketCount i = 0;
         i < (packets_between_probes_base << num_probes) - 1; ++i) {
      SendStreamDataToPeer(3, ".", stream_offset++, NO_FIN, nullptr);
      ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
    }

    // Trigger the next probe.
    SendStreamDataToPeer(3, "!", stream_offset++, NO_FIN, nullptr);
    ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
    QuicByteCount new_probe_size;
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
        .WillOnce(SaveArg<3>(&new_probe_size));
    connection_.GetMtuDiscoveryAlarm()->Fire();
    EXPECT_THAT(new_probe_size, InRange(probe_size, mtu_limit));
    EXPECT_EQ(num_probes + 1, connection_.mtu_probe_count());

    // Acknowledge all packets sent so far.
    QuicAckFrame probe_ack = InitAckFrame(creator_->packet_number());
    ProcessAckPacket(&probe_ack);
    EXPECT_EQ(new_probe_size, connection_.max_packet_length());
    EXPECT_EQ(0u, connection_.GetBytesInFlight());

    probe_size = new_probe_size;
  }

  // The last probe size should be equal to the target.
  EXPECT_EQ(probe_size, mtu_limit);
}

// Tests whether MTU discovery works when the writer returns an error despite
// advertising higher packet length.
TEST_P(QuicConnectionTest, MtuDiscoveryWriterFailed) {
  MtuDiscoveryTestInit();

  const QuicByteCount mtu_limit = kMtuDiscoveryTargetPacketSizeHigh - 1;
  const QuicByteCount initial_mtu = connection_.max_packet_length();
  EXPECT_LT(initial_mtu, mtu_limit);
  writer_->set_max_packet_size(mtu_limit);

  const QuicPacketCount packets_between_probes_base = 5;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  // Send enough packets so that the next one triggers path MTU discovery.
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Trigger the probe.
  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  ASSERT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  writer_->SimulateNextPacketTooLarge();
  connection_.GetMtuDiscoveryAlarm()->Fire();
  ASSERT_TRUE(connection_.connected());

  // Send more data.
  QuicPacketNumber probe_number = creator_->packet_number();
  QuicPacketCount extra_packets = packets_between_probes_base * 3;
  for (QuicPacketCount i = 0; i < extra_packets; i++) {
    connection_.EnsureWritableAndSendStreamData5();
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  // Acknowledge all packets sent so far, except for the lost probe.
  QuicAckFrame probe_ack =
      ConstructAckFrame(creator_->packet_number(), probe_number);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&probe_ack);
  EXPECT_EQ(initial_mtu, connection_.max_packet_length());

  // Send more packets, and ensure that none of them sets the alarm.
  for (QuicPacketCount i = 0; i < 4 * packets_between_probes_base; i++) {
    connection_.EnsureWritableAndSendStreamData5();
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  EXPECT_EQ(initial_mtu, connection_.max_packet_length());
  EXPECT_EQ(1u, connection_.mtu_probe_count());
}

TEST_P(QuicConnectionTest, NoMtuDiscoveryAfterConnectionClosed) {
  MtuDiscoveryTestInit();

  const QuicPacketCount packets_between_probes_base = 10;
  set_packets_between_probes_base(packets_between_probes_base);

  connection_.EnablePathMtuDiscovery(send_algorithm_);

  // Send enough packets so that the next one triggers path MTU discovery.
  for (QuicPacketCount i = 0; i < packets_between_probes_base - 1; i++) {
    SendStreamDataToPeer(3, ".", i, NO_FIN, nullptr);
    ASSERT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
  }

  SendStreamDataToPeer(3, "!", packets_between_probes_base - 1, NO_FIN,
                       nullptr);
  EXPECT_TRUE(connection_.GetMtuDiscoveryAlarm()->IsSet());

  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  connection_.CloseConnection(QUIC_PEER_GOING_AWAY, "no reason",
                              ConnectionCloseBehavior::SILENT_CLOSE);
  EXPECT_FALSE(connection_.GetMtuDiscoveryAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, TimeoutAfterSendDuringHandshake) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);

  const QuicTime::Delta initial_idle_timeout =
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  QuicTime default_timeout = clock_.ApproximateNow() + initial_idle_timeout;

  // When we send a packet, the timeout will change to 5ms +
  // kInitialIdleTimeoutSecs.
  clock_.AdvanceTime(five_ms);
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // Now send more data. This will not move the timeout because
  // no data has been received since the previous write.
  clock_.AdvanceTime(five_ms);
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      3, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // The original alarm will fire.  We should not time out because we had a
  // network event at t=5ms.  The alarm will reregister.
  clock_.AdvanceTime(initial_idle_timeout - five_ms - five_ms);
  EXPECT_EQ(default_timeout, clock_.ApproximateNow());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_TRUE(connection_.connected());
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // This time, we should time out.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  clock_.AdvanceTime(five_ms);
  EXPECT_EQ(default_timeout + five_ms, clock_.ApproximateNow());
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, TimeoutAfterRetransmission) {
  if (connection_.PtoEnabled()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);

  const QuicTime start_time = clock_.Now();
  const QuicTime::Delta initial_idle_timeout =
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  QuicTime default_timeout = clock_.Now() + initial_idle_timeout;

  connection_.SetMaxTailLossProbes(0);
  const QuicTime default_retransmission_time =
      start_time + DefaultRetransmissionTime();

  ASSERT_LT(default_retransmission_time, default_timeout);

  // When we send a packet, the timeout will change to 5 ms +
  // kInitialIdleTimeoutSecs (but it will not reschedule the alarm).
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  const QuicTime send_time = start_time + five_ms;
  clock_.AdvanceTime(five_ms);
  ASSERT_EQ(send_time, clock_.Now());
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // Move forward 5 ms and receive a packet, which will move the timeout
  // forward 5 ms more (but will not reschedule the alarm).
  const QuicTime receive_time = send_time + five_ms;
  clock_.AdvanceTime(receive_time - clock_.Now());
  ASSERT_EQ(receive_time, clock_.Now());
  ProcessPacket(1);

  // Now move forward to the retransmission time and retransmit the
  // packet, which should move the timeout forward again (but will not
  // reschedule the alarm).
  EXPECT_EQ(default_retransmission_time + five_ms,
            connection_.GetRetransmissionAlarm()->deadline());
  // Simulate the retransmission alarm firing.
  const QuicTime rto_time = send_time + DefaultRetransmissionTime();
  const QuicTime final_timeout = rto_time + initial_idle_timeout;
  clock_.AdvanceTime(rto_time - clock_.Now());
  ASSERT_EQ(rto_time, clock_.Now());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(2u), _, _));
  connection_.GetRetransmissionAlarm()->Fire();

  // Advance to the original timeout and fire the alarm. The connection should
  // timeout, and the alarm should be registered based on the time of the
  // retransmission.
  clock_.AdvanceTime(default_timeout - clock_.Now());
  ASSERT_EQ(default_timeout.ToDebuggingValue(),
            clock_.Now().ToDebuggingValue());
  EXPECT_EQ(default_timeout, clock_.Now());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_TRUE(connection_.connected());
  ASSERT_EQ(final_timeout.ToDebuggingValue(),
            connection_.GetTimeoutAlarm()->deadline().ToDebuggingValue());

  // This time, we should time out.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  clock_.AdvanceTime(final_timeout - clock_.Now());
  EXPECT_EQ(connection_.GetTimeoutAlarm()->deadline(), clock_.Now());
  EXPECT_EQ(final_timeout, clock_.Now());
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, TimeoutAfterSendAfterHandshake) {
  // When the idle timeout fires, verify that by default we do not send any
  // connection close packets.
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;

  // Create a handshake message that also enables silent close.
  CryptoHandshakeMessage msg;
  std::string error_details;
  QuicConfig client_config;
  client_config.SetInitialStreamFlowControlWindowToSend(
      kInitialStreamFlowControlWindowForTest);
  client_config.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest);
  client_config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs));
  client_config.ToHandshakeMessage(&msg, connection_.transport_version());
  const QuicErrorCode error =
      config.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());

  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  connection_.SetFromConfig(config);

  const QuicTime::Delta default_idle_timeout =
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs - 1);
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  QuicTime default_timeout = clock_.ApproximateNow() + default_idle_timeout;

  // When we send a packet, the timeout will change to 5ms +
  // kInitialIdleTimeoutSecs.
  clock_.AdvanceTime(five_ms);
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // Now send more data. This will not move the timeout because
  // no data has been received since the previous write.
  clock_.AdvanceTime(five_ms);
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      3, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // The original alarm will fire.  We should not time out because we had a
  // network event at t=5ms.  The alarm will reregister.
  clock_.AdvanceTime(default_idle_timeout - five_ms - five_ms);
  EXPECT_EQ(default_timeout, clock_.ApproximateNow());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_TRUE(connection_.connected());
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // This time, we should time out.
  // This results in a SILENT_CLOSE, so the writer will not be invoked
  // and will not save the frame. Grab the frame from OnConnectionClosed
  // directly.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));

  clock_.AdvanceTime(five_ms);
  EXPECT_EQ(default_timeout + five_ms, clock_.ApproximateNow());
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_NETWORK_IDLE_TIMEOUT));
}

TEST_P(QuicConnectionTest, TimeoutAfterSendSilentCloseAndTLP) {
  if (connection_.PtoEnabled()) {
    return;
  }
  // Same test as above, but sending TLPs causes a connection close to be sent.
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;

  // Create a handshake message that also enables silent close.
  CryptoHandshakeMessage msg;
  std::string error_details;
  QuicConfig client_config;
  client_config.SetInitialStreamFlowControlWindowToSend(
      kInitialStreamFlowControlWindowForTest);
  client_config.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest);
  client_config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs));
  client_config.ToHandshakeMessage(&msg, connection_.transport_version());
  const QuicErrorCode error =
      config.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());

  connection_.SetFromConfig(config);

  const QuicTime::Delta default_idle_timeout =
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs - 1);
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  QuicTime default_timeout = clock_.ApproximateNow() + default_idle_timeout;

  // When we send a packet, the timeout will change to 5ms +
  // kInitialIdleTimeoutSecs.
  clock_.AdvanceTime(five_ms);
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // Retransmit the packet via tail loss probe.
  clock_.AdvanceTime(connection_.GetRetransmissionAlarm()->deadline() -
                     clock_.Now());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(2u), _, _));
  connection_.GetRetransmissionAlarm()->Fire();

  // This time, we should time out and send a connection close due to the TLP.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  clock_.AdvanceTime(connection_.GetTimeoutAlarm()->deadline() -
                     clock_.ApproximateNow() + five_ms);
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, TimeoutAfterSendSilentCloseWithOpenStreams) {
  // Same test as above, but having open streams causes a connection close
  // to be sent.
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;

  // Create a handshake message that also enables silent close.
  CryptoHandshakeMessage msg;
  std::string error_details;
  QuicConfig client_config;
  client_config.SetInitialStreamFlowControlWindowToSend(
      kInitialStreamFlowControlWindowForTest);
  client_config.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest);
  client_config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs));
  client_config.ToHandshakeMessage(&msg, connection_.transport_version());
  const QuicErrorCode error =
      config.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());

  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  connection_.SetFromConfig(config);

  const QuicTime::Delta default_idle_timeout =
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs - 1);
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  QuicTime default_timeout = clock_.ApproximateNow() + default_idle_timeout;

  // When we send a packet, the timeout will change to 5ms +
  // kInitialIdleTimeoutSecs.
  clock_.AdvanceTime(five_ms);
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // Indicate streams are still open.
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));
  if (GetQuicReloadableFlag(quic_add_stream_info_to_idle_close_detail)) {
    EXPECT_CALL(visitor_, GetStreamsInfoForLogging()).WillOnce(Return(""));
  }

  // This time, we should time out and send a connection close due to the TLP.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  clock_.AdvanceTime(connection_.GetTimeoutAlarm()->deadline() -
                     clock_.ApproximateNow() + five_ms);
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, TimeoutAfterReceive) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);

  const QuicTime::Delta initial_idle_timeout =
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  QuicTime default_timeout = clock_.ApproximateNow() + initial_idle_timeout;

  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, NO_FIN);
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      3, NO_FIN);

  EXPECT_EQ(default_timeout, connection_.GetTimeoutAlarm()->deadline());
  clock_.AdvanceTime(five_ms);

  // When we receive a packet, the timeout will change to 5ms +
  // kInitialIdleTimeoutSecs.
  QuicAckFrame ack = InitAckFrame(2);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&ack);

  // The original alarm will fire.  We should not time out because we had a
  // network event at t=5ms.  The alarm will reregister.
  clock_.AdvanceTime(initial_idle_timeout - five_ms);
  EXPECT_EQ(default_timeout, clock_.ApproximateNow());
  EXPECT_TRUE(connection_.connected());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // This time, we should time out.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  clock_.AdvanceTime(five_ms);
  EXPECT_EQ(default_timeout + five_ms, clock_.ApproximateNow());
  connection_.GetTimeoutAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, TimeoutAfterReceiveNotSendWhenUnacked) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);

  const QuicTime::Delta initial_idle_timeout =
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  connection_.SetNetworkTimeouts(
      QuicTime::Delta::Infinite(),
      initial_idle_timeout + QuicTime::Delta::FromSeconds(1));
  const QuicTime::Delta five_ms = QuicTime::Delta::FromMilliseconds(5);
  QuicTime default_timeout = clock_.ApproximateNow() + initial_idle_timeout;

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, NO_FIN);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      3, NO_FIN);

  EXPECT_EQ(default_timeout, connection_.GetTimeoutAlarm()->deadline());

  clock_.AdvanceTime(five_ms);

  // When we receive a packet, the timeout will change to 5ms +
  // kInitialIdleTimeoutSecs.
  QuicAckFrame ack = InitAckFrame(2);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&ack);

  // The original alarm will fire.  We should not time out because we had a
  // network event at t=5ms.  The alarm will reregister.
  clock_.AdvanceTime(initial_idle_timeout - five_ms);
  EXPECT_EQ(default_timeout, clock_.ApproximateNow());
  EXPECT_TRUE(connection_.connected());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_EQ(default_timeout + five_ms,
            connection_.GetTimeoutAlarm()->deadline());

  // Now, send packets while advancing the time and verify that the connection
  // eventually times out.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());
  for (int i = 0; i < 100 && connection_.connected(); ++i) {
    QUIC_LOG(INFO) << "sending data packet";
    connection_.SendStreamDataWithString(
        GetNthClientInitiatedStreamId(1, connection_.transport_version()),
        "foo", 0, NO_FIN);
    connection_.GetTimeoutAlarm()->Fire();
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  }
  EXPECT_FALSE(connection_.connected());
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  TestConnectionCloseQuicErrorCode(QUIC_NETWORK_IDLE_TIMEOUT);
}

TEST_P(QuicConnectionTest, TimeoutAfter5ClientRTOs) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(2);
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k5RTO);
  config.SetConnectionOptionsToSend(connection_options);
  QuicConfigPeer::SetNegotiated(&config, true);
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  }
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  connection_.SetFromConfig(config);

  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);

  // Fire the retransmission alarm 6 times, twice for TLP and 4 times for RTO.
  for (int i = 0; i < 6; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    connection_.GetRetransmissionAlarm()->Fire();
    EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
    EXPECT_TRUE(connection_.connected());
  }
  EXPECT_CALL(visitor_, OnPathDegrading());
  connection_.PathDegradingTimeout();

  EXPECT_EQ(2u, connection_.sent_packet_manager().GetConsecutiveTlpCount());
  EXPECT_EQ(4u, connection_.sent_packet_manager().GetConsecutiveRtoCount());
  // This time, we should time out.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  ASSERT_TRUE(connection_.BlackholeDetectionInProgress());
  connection_.GetBlackholeDetectorAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_TOO_MANY_RTOS);
}

TEST_P(QuicConnectionTest, SendScheduler) {
  // Test that if we send a packet without delay, it is not queued.
  QuicFramerPeer::SetPerspective(&peer_framer_, Perspective::IS_CLIENT);
  std::unique_ptr<QuicPacket> packet =
      ConstructDataPacket(1, !kHasStopWaiting, ENCRYPTION_INITIAL);
  QuicPacketCreatorPeer::SetPacketNumber(creator_, 1);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  connection_.SendPacket(ENCRYPTION_INITIAL, 1, std::move(packet),
                         HAS_RETRANSMITTABLE_DATA, false, false);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_P(QuicConnectionTest, FailToSendFirstPacket) {
  // Test that the connection does not crash when it fails to send the first
  // packet at which point self_address_ might be uninitialized.
  QuicFramerPeer::SetPerspective(&peer_framer_, Perspective::IS_CLIENT);
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(1);
  std::unique_ptr<QuicPacket> packet =
      ConstructDataPacket(1, !kHasStopWaiting, ENCRYPTION_INITIAL);
  QuicPacketCreatorPeer::SetPacketNumber(creator_, 1);
  writer_->SetShouldWriteFail();
  connection_.SendPacket(ENCRYPTION_INITIAL, 1, std::move(packet),
                         HAS_RETRANSMITTABLE_DATA, false, false);
}

TEST_P(QuicConnectionTest, SendSchedulerEAGAIN) {
  QuicFramerPeer::SetPerspective(&peer_framer_, Perspective::IS_CLIENT);
  std::unique_ptr<QuicPacket> packet =
      ConstructDataPacket(1, !kHasStopWaiting, ENCRYPTION_INITIAL);
  QuicPacketCreatorPeer::SetPacketNumber(creator_, 1);
  BlockOnNextWrite();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(2u), _, _))
      .Times(0);
  connection_.SendPacket(ENCRYPTION_INITIAL, 1, std::move(packet),
                         HAS_RETRANSMITTABLE_DATA, false, false);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
}

TEST_P(QuicConnectionTest, TestQueueLimitsOnSendStreamData) {
  // Queue the first packet.
  size_t payload_length = connection_.max_packet_length();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(testing::Return(false));
  const std::string payload(payload_length, 'a');
  QuicStreamId first_bidi_stream_id(QuicUtils::GetFirstBidirectionalStreamId(
      connection_.version().transport_version, Perspective::IS_CLIENT));
  EXPECT_EQ(0u, connection_
                    .SendStreamDataWithString(first_bidi_stream_id, payload, 0,
                                              NO_FIN)
                    .bytes_consumed);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_P(QuicConnectionTest, SendingThreePackets) {
  // Make the payload twice the size of the packet, so 3 packets are written.
  size_t total_payload_length = 2 * connection_.max_packet_length();
  const std::string payload(total_payload_length, 'a');
  QuicStreamId first_bidi_stream_id(QuicUtils::GetFirstBidirectionalStreamId(
      connection_.version().transport_version, Perspective::IS_CLIENT));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(3);
  EXPECT_EQ(payload.size(), connection_
                                .SendStreamDataWithString(first_bidi_stream_id,
                                                          payload, 0, NO_FIN)
                                .bytes_consumed);
}

TEST_P(QuicConnectionTest, LoopThroughSendingPacketsWithTruncation) {
  set_perspective(Perspective::IS_SERVER);
  if (!GetParam().version.HasIetfInvariantHeader()) {
    // For IETF QUIC, encryption level will be switched to FORWARD_SECURE in
    // SendStreamDataWithString.
    QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  }
  // Set up a larger payload than will fit in one packet.
  const std::string payload(connection_.max_packet_length(), 'a');
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _)).Times(AnyNumber());

  // Now send some packets with no truncation.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  EXPECT_EQ(payload.size(),
            connection_.SendStreamDataWithString(3, payload, 0, NO_FIN)
                .bytes_consumed);
  // Track the size of the second packet here.  The overhead will be the largest
  // we see in this test, due to the non-truncated connection id.
  size_t non_truncated_packet_size = writer_->last_packet_size();

  // Change to a 0 byte connection id.
  QuicConfig config;
  QuicConfigPeer::SetReceivedBytesForConnectionId(&config, 0);
  connection_.SetFromConfig(config);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  EXPECT_EQ(payload.size(),
            connection_.SendStreamDataWithString(3, payload, 1350, NO_FIN)
                .bytes_consumed);
  if (connection_.version().HasIetfInvariantHeader()) {
    // Short header packets sent from server omit connection ID already, and
    // stream offset size increases from 0 to 2.
    EXPECT_EQ(non_truncated_packet_size, writer_->last_packet_size() - 2);
  } else {
    // Just like above, we save 8 bytes on payload, and 8 on truncation. -2
    // because stream offset size is 2 instead of 0.
    EXPECT_EQ(non_truncated_packet_size,
              writer_->last_packet_size() + 8 * 2 - 2);
  }
}

TEST_P(QuicConnectionTest, SendDelayedAck) {
  QuicTime ack_time = clock_.ApproximateNow() + DefaultDelayedAckTime();
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.HasPendingAcks());
  const uint8_t tag = 0x07;
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(tag));
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(tag));
  // Process a packet from the non-crypto stream.
  frame1_.stream_id = 3;

  // The same as ProcessPacket(1) except that ENCRYPTION_ZERO_RTT is used
  // instead of ENCRYPTION_INITIAL.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);

  // Check if delayed ack timer is running for the expected interval.
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());
  // Simulate delayed ack alarm firing.
  clock_.AdvanceTime(DefaultDelayedAckTime());
  connection_.GetAckAlarm()->Fire();
  // Check that ack is sent and that delayed ack alarm is reset.
  size_t padding_frame_count = writer_->padding_frames().size();
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(padding_frame_count + 2u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(writer_->ack_frames().empty());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, SendDelayedAckDecimation) {
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame()).Times(AnyNumber());

  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  // The ack time should be based on min_rtt/4, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() +
                      QuicTime::Delta::FromMilliseconds(kMinRttMs / 4);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.HasPendingAcks());
  const uint8_t tag = 0x07;
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(tag));
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(tag));
  // Process a packet from the non-crypto stream.
  frame1_.stream_id = 3;

  // Process all the initial packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (unsigned int i = 0; i < kFirstDecimatedPacket - 1; ++i) {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacketAtLevel(1 + i, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  }
  EXPECT_FALSE(connection_.HasPendingAcks());
  // The same as ProcessPacket(1) except that ENCRYPTION_ZERO_RTT is used
  // instead of ENCRYPTION_INITIAL.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(kFirstDecimatedPacket, !kHasStopWaiting,
                           ENCRYPTION_ZERO_RTT);

  // Check if delayed ack timer is running for the expected interval.
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());

  // The 10th received packet causes an ack to be sent.
  for (int i = 0; i < 9; ++i) {
    EXPECT_TRUE(connection_.HasPendingAcks());
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacketAtLevel(kFirstDecimatedPacket + 1 + i, !kHasStopWaiting,
                             ENCRYPTION_ZERO_RTT);
  }
  // Check that ack is sent and that delayed ack alarm is reset.
  size_t padding_frame_count = writer_->padding_frames().size();
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(padding_frame_count + 2u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(writer_->ack_frames().empty());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, SendDelayedAckDecimationUnlimitedAggregation) {
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kACKD);
  // No limit on the number of packets received before sending an ack.
  connection_options.push_back(kAKDU);
  config.SetConnectionOptionsToSend(connection_options);
  connection_.SetFromConfig(config);

  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  // The ack time should be based on min_rtt/4, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() +
                      QuicTime::Delta::FromMilliseconds(kMinRttMs / 4);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.HasPendingAcks());
  const uint8_t tag = 0x07;
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(tag));
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(tag));
  // Process a packet from the non-crypto stream.
  frame1_.stream_id = 3;

  // Process all the initial packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (unsigned int i = 0; i < kFirstDecimatedPacket - 1; ++i) {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacketAtLevel(1 + i, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  }
  EXPECT_FALSE(connection_.HasPendingAcks());
  // The same as ProcessPacket(1) except that ENCRYPTION_ZERO_RTT is used
  // instead of ENCRYPTION_INITIAL.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(kFirstDecimatedPacket, !kHasStopWaiting,
                           ENCRYPTION_ZERO_RTT);

  // Check if delayed ack timer is running for the expected interval.
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());

  // 18 packets will not cause an ack to be sent.  19 will because when
  // stop waiting frames are in use, we ack every 20 packets no matter what.
  for (int i = 0; i < 18; ++i) {
    EXPECT_TRUE(connection_.HasPendingAcks());
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacketAtLevel(kFirstDecimatedPacket + 1 + i, !kHasStopWaiting,
                             ENCRYPTION_ZERO_RTT);
  }
  // The delayed ack timer should still be set to the expected deadline.
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());
}

TEST_P(QuicConnectionTest, SendDelayedAckDecimationEighthRtt) {
  EXPECT_CALL(visitor_, OnAckNeedsRetransmittableFrame()).Times(AnyNumber());
  QuicConnectionPeer::SetAckDecimationDelay(&connection_, 0.125);

  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  // The ack time should be based on min_rtt/8, since it's less than the
  // default delayed ack time.
  QuicTime ack_time = clock_.ApproximateNow() +
                      QuicTime::Delta::FromMilliseconds(kMinRttMs / 8);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.HasPendingAcks());
  const uint8_t tag = 0x07;
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(tag));
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(tag));
  // Process a packet from the non-crypto stream.
  frame1_.stream_id = 3;

  // Process all the initial packets in order so there aren't missing packets.
  uint64_t kFirstDecimatedPacket = 101;
  for (unsigned int i = 0; i < kFirstDecimatedPacket - 1; ++i) {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacketAtLevel(1 + i, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  }
  EXPECT_FALSE(connection_.HasPendingAcks());
  // The same as ProcessPacket(1) except that ENCRYPTION_ZERO_RTT is used
  // instead of ENCRYPTION_INITIAL.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(kFirstDecimatedPacket, !kHasStopWaiting,
                           ENCRYPTION_ZERO_RTT);

  // Check if delayed ack timer is running for the expected interval.
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());

  // The 10th received packet causes an ack to be sent.
  for (int i = 0; i < 9; ++i) {
    EXPECT_TRUE(connection_.HasPendingAcks());
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    ProcessDataPacketAtLevel(kFirstDecimatedPacket + 1 + i, !kHasStopWaiting,
                             ENCRYPTION_ZERO_RTT);
  }
  // Check that ack is sent and that delayed ack alarm is reset.
  size_t padding_frame_count = writer_->padding_frames().size();
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(padding_frame_count + 2u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(writer_->ack_frames().empty());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, SendDelayedAckOnHandshakeConfirmed) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  ProcessPacket(1);
  // Check that ack is sent and that delayed ack alarm is set.
  EXPECT_TRUE(connection_.HasPendingAcks());
  QuicTime ack_time = clock_.ApproximateNow() + DefaultDelayedAckTime();
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());

  // Completing the handshake as the server does nothing.
  QuicConnectionPeer::SetPerspective(&connection_, Perspective::IS_SERVER);
  connection_.OnHandshakeComplete();
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(ack_time, connection_.GetAckAlarm()->deadline());

  // Complete the handshake as the client decreases the delayed ack time to 0ms.
  QuicConnectionPeer::SetPerspective(&connection_, Perspective::IS_CLIENT);
  connection_.OnHandshakeComplete();
  EXPECT_TRUE(connection_.HasPendingAcks());
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    EXPECT_EQ(clock_.ApproximateNow() + DefaultDelayedAckTime(),
              connection_.GetAckAlarm()->deadline());
  } else {
    EXPECT_EQ(clock_.ApproximateNow(), connection_.GetAckAlarm()->deadline());
  }
}

TEST_P(QuicConnectionTest, SendDelayedAckOnSecondPacket) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  ProcessPacket(1);
  ProcessPacket(2);
  // Check that ack is sent and that delayed ack alarm is reset.
  size_t padding_frame_count = writer_->padding_frames().size();
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(padding_frame_count + 2u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(writer_->ack_frames().empty());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, NoAckOnOldNacks) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessPacket(2);
  size_t frames_per_ack = GetParam().no_stop_waiting ? 1 : 2;

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessPacket(3);
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + frames_per_ack, writer_->frame_count());
  EXPECT_FALSE(writer_->ack_frames().empty());
  writer_->Reset();

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessPacket(4);
  EXPECT_EQ(0u, writer_->frame_count());

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessPacket(5);
  padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + frames_per_ack, writer_->frame_count());
  EXPECT_FALSE(writer_->ack_frames().empty());
  writer_->Reset();

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  // Now only set the timer on the 6th packet, instead of sending another ack.
  ProcessPacket(6);
  padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count, writer_->frame_count());
  EXPECT_TRUE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, SendDelayedAckOnOutgoingPacket) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_));
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0x01));
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypter>(0x01));
  ProcessDataPacket(1);
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, NO_FIN);
  // Check that ack is bundled with outgoing data and that delayed ack
  // alarm is reset.
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(2u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(3u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(writer_->ack_frames().empty());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, SendDelayedAckOnOutgoingCryptoPacket) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  }
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);
  connection_.SendCryptoDataWithString("foo", 0);
  // Check that ack is bundled with outgoing crypto data.
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(3u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(4u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, BlockAndBufferOnFirstCHLOPacketOfTwo) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  ProcessPacket(1);
  BlockOnNextWrite();
  writer_->set_is_write_blocked_data_buffered(true);
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  } else {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  }
  connection_.SendCryptoDataWithString("foo", 0);
  EXPECT_TRUE(writer_->IsWriteBlocked());
  EXPECT_FALSE(connection_.HasQueuedData());
  connection_.SendCryptoDataWithString("bar", 3);
  EXPECT_TRUE(writer_->IsWriteBlocked());
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    // CRYPTO frames are not flushed when writer is blocked.
    EXPECT_FALSE(connection_.HasQueuedData());
  } else {
    EXPECT_TRUE(connection_.HasQueuedData());
  }
}

TEST_P(QuicConnectionTest, BundleAckForSecondCHLO) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.HasPendingAcks());
  EXPECT_CALL(visitor_, OnCanWrite())
      .WillOnce(IgnoreResult(InvokeWithoutArgs(
          &connection_, &TestConnection::SendCryptoStreamData)));
  // Process a packet from the crypto stream, which is frame1_'s default.
  // Receiving the CHLO as packet 2 first will cause the connection to
  // immediately send an ack, due to the packet gap.
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  }
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_INITIAL);
  // Check that ack is sent and that delayed ack alarm is reset.
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(3u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(4u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  if (!QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_EQ(1u, writer_->stream_frames().size());
  } else {
    EXPECT_EQ(1u, writer_->crypto_frames().size());
  }
  EXPECT_EQ(1u, writer_->padding_frames().size());
  ASSERT_FALSE(writer_->ack_frames().empty());
  EXPECT_EQ(QuicPacketNumber(2u), LargestAcked(writer_->ack_frames().front()));
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, BundleAckForSecondCHLOTwoPacketReject) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.HasPendingAcks());

  // Process two packets from the crypto stream, which is frame1_'s default,
  // simulating a 2 packet reject.
  {
    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
    } else {
      EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
    }
    ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);
    // Send the new CHLO when the REJ is processed.
    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      EXPECT_CALL(visitor_, OnCryptoFrame(_))
          .WillOnce(IgnoreResult(InvokeWithoutArgs(
              &connection_, &TestConnection::SendCryptoStreamData)));
    } else {
      EXPECT_CALL(visitor_, OnStreamFrame(_))
          .WillOnce(IgnoreResult(InvokeWithoutArgs(
              &connection_, &TestConnection::SendCryptoStreamData)));
    }
    ProcessCryptoPacketAtLevel(2, ENCRYPTION_INITIAL);
  }
  // Check that ack is sent and that delayed ack alarm is reset.
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(3u, writer_->frame_count());
    EXPECT_TRUE(writer_->stop_waiting_frames().empty());
  } else {
    EXPECT_EQ(4u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  if (!QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_EQ(1u, writer_->stream_frames().size());
  } else {
    EXPECT_EQ(1u, writer_->crypto_frames().size());
  }
  EXPECT_EQ(1u, writer_->padding_frames().size());
  ASSERT_FALSE(writer_->ack_frames().empty());
  EXPECT_EQ(QuicPacketNumber(2u), LargestAcked(writer_->ack_frames().front()));
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, BundleAckWithDataOnIncomingAck) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, NO_FIN);
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      3, NO_FIN);
  // Ack the second packet, which will retransmit the first packet.
  QuicAckFrame ack = ConstructAckFrame(2, 1);
  LostPacketVector lost_packets;
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(1), kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&ack);
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->stream_frames().size());
  writer_->Reset();

  // Now ack the retransmission, which will both raise the high water mark
  // and see if there is more data to send.
  ack = ConstructAckFrame(3, 1);
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(&ack);

  // Check that no packet is sent and the ack alarm isn't set.
  EXPECT_EQ(0u, writer_->frame_count());
  EXPECT_FALSE(connection_.HasPendingAcks());
  writer_->Reset();

  // Send the same ack, but send both data and an ack together.
  ack = ConstructAckFrame(3, 1);
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(visitor_, OnCanWrite())
      .WillOnce(IgnoreResult(InvokeWithoutArgs(
          &connection_, &TestConnection::EnsureWritableAndSendStreamData5)));
  ProcessAckPacket(&ack);

  // Check that ack is bundled with outgoing data and the delayed ack
  // alarm is reset.
  if (GetParam().no_stop_waiting) {
    // Do not ACK acks.
    EXPECT_EQ(1u, writer_->frame_count());
  } else {
    EXPECT_EQ(3u, writer_->frame_count());
    EXPECT_FALSE(writer_->stop_waiting_frames().empty());
  }
  if (GetParam().no_stop_waiting) {
    EXPECT_TRUE(writer_->ack_frames().empty());
  } else {
    EXPECT_FALSE(writer_->ack_frames().empty());
    EXPECT_EQ(QuicPacketNumber(3u),
              LargestAcked(writer_->ack_frames().front()));
  }
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, NoAckSentForClose) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  ProcessPacket(1);
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_PEER))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  ProcessClosePacket(2);
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PEER_GOING_AWAY));
}

TEST_P(QuicConnectionTest, SendWhenDisconnected) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  connection_.CloseConnection(QUIC_PEER_GOING_AWAY, "no reason",
                              ConnectionCloseBehavior::SILENT_CLOSE);
  EXPECT_FALSE(connection_.connected());
  EXPECT_FALSE(connection_.CanWrite(HAS_RETRANSMITTABLE_DATA));
  EXPECT_EQ(DISCARD, connection_.GetSerializedPacketFate(
                         /*is_mtu_discovery=*/false, ENCRYPTION_INITIAL));
}

TEST_P(QuicConnectionTest, SendConnectivityProbingWhenDisconnected) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  connection_.CloseConnection(QUIC_PEER_GOING_AWAY, "no reason",
                              ConnectionCloseBehavior::SILENT_CLOSE);
  EXPECT_FALSE(connection_.connected());
  EXPECT_FALSE(connection_.CanWrite(HAS_RETRANSMITTABLE_DATA));

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(1), _, _))
      .Times(0);

  EXPECT_QUIC_BUG(connection_.SendConnectivityProbingPacket(
                      writer_.get(), connection_.peer_address()),
                  "Not sending connectivity probing packet as connection is "
                  "disconnected.");
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PEER_GOING_AWAY));
}

TEST_P(QuicConnectionTest, WriteBlockedAfterClientSendsConnectivityProbe) {
  PathProbeTestInit(Perspective::IS_CLIENT);
  TestPacketWriter probing_writer(version(), &clock_, Perspective::IS_CLIENT);
  // Block next write so that sending connectivity probe will encounter a
  // blocked write when send a connectivity probe to the peer.
  probing_writer.BlockOnNextWrite();
  // Connection will not be marked as write blocked as connectivity probe only
  // affects the probing_writer which is not the default.
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(0);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(1), _, _))
      .Times(1);
  connection_.SendConnectivityProbingPacket(&probing_writer,
                                            connection_.peer_address());
}

TEST_P(QuicConnectionTest, WriterBlockedAfterServerSendsConnectivityProbe) {
  PathProbeTestInit(Perspective::IS_SERVER);
  if (version().SupportsAntiAmplificationLimit()) {
    QuicConnectionPeer::SetAddressValidated(&connection_);
  }

  // Block next write so that sending connectivity probe will encounter a
  // blocked write when send a connectivity probe to the peer.
  writer_->BlockOnNextWrite();
  // Connection will be marked as write blocked as server uses the default
  // writer to send connectivity probes.
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(1);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(1), _, _))
      .Times(1);
  if (connection_.send_path_response() &&
      VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    QuicPathFrameBuffer payload{
        {0xde, 0xad, 0xbe, 0xef, 0xba, 0xdc, 0x0f, 0xfe}};
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendPathChallenge(
        payload, connection_.self_address(), connection_.peer_address(),
        connection_.effective_peer_address(), writer_.get());
  } else {
    connection_.SendConnectivityProbingPacket(writer_.get(),
                                              connection_.peer_address());
  }
}

TEST_P(QuicConnectionTest, WriterErrorWhenClientSendsConnectivityProbe) {
  PathProbeTestInit(Perspective::IS_CLIENT);
  TestPacketWriter probing_writer(version(), &clock_, Perspective::IS_CLIENT);
  probing_writer.SetShouldWriteFail();

  // Connection should not be closed if a connectivity probe is failed to be
  // sent.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(1), _, _))
      .Times(0);
  connection_.SendConnectivityProbingPacket(&probing_writer,
                                            connection_.peer_address());
}

TEST_P(QuicConnectionTest, WriterErrorWhenServerSendsConnectivityProbe) {
  PathProbeTestInit(Perspective::IS_SERVER);

  writer_->SetShouldWriteFail();
  // Connection should not be closed if a connectivity probe is failed to be
  // sent.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(1), _, _))
      .Times(0);
  connection_.SendConnectivityProbingPacket(writer_.get(),
                                            connection_.peer_address());
}

TEST_P(QuicConnectionTest, PublicReset) {
  if (GetParam().version.HasIetfInvariantHeader()) {
    return;
  }
  QuicPublicResetPacket header;
  // Public reset packet in only built by server.
  header.connection_id = connection_id_;
  std::unique_ptr<QuicEncryptedPacket> packet(
      framer_.BuildPublicResetPacket(header));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*packet, QuicTime::Zero()));
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_PEER))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  connection_.ProcessUdpPacket(kSelfAddress, kPeerAddress, *received);
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PUBLIC_RESET));
}

TEST_P(QuicConnectionTest, IetfStatelessReset) {
  if (!GetParam().version.HasIetfInvariantHeader()) {
    return;
  }
  const QuicUint128 kTestStatelessResetToken = 1010101;
  QuicConfig config;
  QuicConfigPeer::SetReceivedStatelessResetToken(&config,
                                                 kTestStatelessResetToken);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildIetfStatelessResetPacket(connection_id_,
                                                kTestStatelessResetToken));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*packet, QuicTime::Zero()));
  if (!connection_.use_path_validator()) {
    EXPECT_CALL(visitor_, ValidateStatelessReset(_, _)).WillOnce(Return(true));
  }
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_PEER))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  connection_.ProcessUdpPacket(kSelfAddress, kPeerAddress, *received);
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PUBLIC_RESET));
}

TEST_P(QuicConnectionTest, GoAway) {
  if (VersionHasIetfQuicFrames(GetParam().version.transport_version)) {
    // GoAway is not available in version 99.
    return;
  }

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  QuicGoAwayFrame* goaway = new QuicGoAwayFrame();
  goaway->last_good_stream_id = 1;
  goaway->error_code = QUIC_PEER_GOING_AWAY;
  goaway->reason_phrase = "Going away.";
  EXPECT_CALL(visitor_, OnGoAway(_));
  ProcessGoAwayPacket(goaway);
}

TEST_P(QuicConnectionTest, WindowUpdate) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  QuicWindowUpdateFrame* window_update = new QuicWindowUpdateFrame();
  window_update->stream_id = 3;
  window_update->max_data = 1234;
  EXPECT_CALL(visitor_, OnWindowUpdateFrame(_));
  ProcessFramePacket(QuicFrame(window_update));
}

TEST_P(QuicConnectionTest, Blocked) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  QuicBlockedFrame* blocked = new QuicBlockedFrame();
  blocked->stream_id = 3;
  EXPECT_CALL(visitor_, OnBlockedFrame(_));
  ProcessFramePacket(QuicFrame(blocked));
  EXPECT_EQ(1u, connection_.GetStats().blocked_frames_received);
  EXPECT_EQ(0u, connection_.GetStats().blocked_frames_sent);
}

TEST_P(QuicConnectionTest, ZeroBytePacket) {
  // Don't close the connection for zero byte packets.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);
  QuicReceivedPacket encrypted(nullptr, 0, QuicTime::Zero());
  connection_.ProcessUdpPacket(kSelfAddress, kPeerAddress, encrypted);
}

TEST_P(QuicConnectionTest, MissingPacketsBeforeLeastUnacked) {
  if (GetParam().version.HasIetfInvariantHeader()) {
    return;
  }
  // Set the packet number of the ack packet to be least unacked (4).
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 3);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  ProcessStopWaitingPacket(InitStopWaitingFrame(4));
  EXPECT_FALSE(connection_.ack_frame().packets.Empty());
}

TEST_P(QuicConnectionTest, ClientHandlesVersionNegotiation) {
  // All supported versions except the one the connection supports.
  ParsedQuicVersionVector versions;
  for (auto version : AllSupportedVersions()) {
    if (version != connection_.version()) {
      versions.push_back(version);
    }
  }

  // Send a version negotiation packet.
  std::unique_ptr<QuicEncryptedPacket> encrypted(
      QuicFramer::BuildVersionNegotiationPacket(
          connection_id_, EmptyQuicConnectionId(),
          connection_.version().HasIetfInvariantHeader(),
          connection_.version().HasLengthPrefixedConnectionIds(), versions));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*encrypted, QuicTime::Zero()));
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  // Verify no connection close packet gets sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.ProcessUdpPacket(kSelfAddress, kPeerAddress, *received);
  EXPECT_FALSE(connection_.connected());
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_INVALID_VERSION));
}

TEST_P(QuicConnectionTest, BadVersionNegotiation) {
  // Send a version negotiation packet with the version the client started with.
  // It should be rejected.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  std::unique_ptr<QuicEncryptedPacket> encrypted(
      QuicFramer::BuildVersionNegotiationPacket(
          connection_id_, EmptyQuicConnectionId(),
          connection_.version().HasIetfInvariantHeader(),
          connection_.version().HasLengthPrefixedConnectionIds(),
          AllSupportedVersions()));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*encrypted, QuicTime::Zero()));
  connection_.ProcessUdpPacket(kSelfAddress, kPeerAddress, *received);
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_INVALID_VERSION_NEGOTIATION_PACKET));
}

TEST_P(QuicConnectionTest, CheckSendStats) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(0);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  connection_.SendStreamDataWithString(3, "first", 0, NO_FIN);
  size_t first_packet_size = writer_->last_packet_size();

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  connection_.SendStreamDataWithString(5, "second", 0, NO_FIN);
  size_t second_packet_size = writer_->last_packet_size();

  // 2 retransmissions due to rto, 1 due to explicit nack.
  EXPECT_CALL(*send_algorithm_, OnRetransmissionTimeout(true));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(3);

  // Retransmit due to RTO.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(10));
  connection_.GetRetransmissionAlarm()->Fire();

  // Retransmit due to explicit nacks.
  QuicAckFrame nack_three =
      InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)},
                    {QuicPacketNumber(4), QuicPacketNumber(5)}});

  LostPacketVector lost_packets;
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(1), kMaxOutgoingPacketSize));
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(3), kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  ProcessAckPacket(&nack_three);

  EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
      .WillOnce(Return(QuicBandwidth::Zero()));

  const QuicConnectionStats& stats = connection_.GetStats();
  // For IETF QUIC, version is not included as the encryption level switches to
  // FORWARD_SECURE in SendStreamDataWithString.
  size_t save_on_version =
      GetParam().version.HasIetfInvariantHeader() ? 0 : kQuicVersionSize;
  EXPECT_EQ(3 * first_packet_size + 2 * second_packet_size - save_on_version,
            stats.bytes_sent);
  EXPECT_EQ(5u, stats.packets_sent);
  EXPECT_EQ(2 * first_packet_size + second_packet_size - save_on_version,
            stats.bytes_retransmitted);
  EXPECT_EQ(3u, stats.packets_retransmitted);
  EXPECT_EQ(1u, stats.rto_count);
  EXPECT_EQ(kDefaultMaxPacketSize, stats.max_packet_size);
}

TEST_P(QuicConnectionTest, ProcessFramesIfPacketClosedConnection) {
  // Construct a packet with stream frame and connection close frame.
  QuicPacketHeader header;
  if (peer_framer_.perspective() == Perspective::IS_SERVER) {
    header.source_connection_id = connection_id_;
    header.destination_connection_id_included = CONNECTION_ID_ABSENT;
    if (!peer_framer_.version().HasIetfInvariantHeader()) {
      header.source_connection_id_included = CONNECTION_ID_PRESENT;
    }
  } else {
    header.destination_connection_id = connection_id_;
    if (peer_framer_.version().HasIetfInvariantHeader()) {
      header.destination_connection_id_included = CONNECTION_ID_ABSENT;
    }
  }
  header.packet_number = QuicPacketNumber(1);
  header.version_flag = false;

  QuicErrorCode kQuicErrorCode = QUIC_PEER_GOING_AWAY;
  // This QuicConnectionCloseFrame will default to being for a Google QUIC
  // close. If doing IETF QUIC then set fields appropriately for CC/T or CC/A,
  // depending on the mapping.
  QuicConnectionCloseFrame qccf(peer_framer_.transport_version(),
                                kQuicErrorCode, NO_IETF_QUIC_ERROR, "",
                                /*transport_close_frame_type=*/0);
  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  frames.push_back(QuicFrame(&qccf));
  std::unique_ptr<QuicPacket> packet(ConstructPacket(header, frames));
  EXPECT_TRUE(nullptr != packet);
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length = peer_framer_.EncryptPayload(
      ENCRYPTION_FORWARD_SECURE, QuicPacketNumber(1), *packet, buffer,
      kMaxOutgoingPacketSize);

  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_PEER))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, encrypted_length, QuicTime::Zero(), false));
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_PEER_GOING_AWAY));
}

TEST_P(QuicConnectionTest, SelectMutualVersion) {
  connection_.SetSupportedVersions(AllSupportedVersions());
  // Set the connection to speak the lowest quic version.
  connection_.set_version(QuicVersionMin());
  EXPECT_EQ(QuicVersionMin(), connection_.version());

  // Pass in available versions which includes a higher mutually supported
  // version.  The higher mutually supported version should be selected.
  ParsedQuicVersionVector supported_versions = AllSupportedVersions();
  EXPECT_TRUE(connection_.SelectMutualVersion(supported_versions));
  EXPECT_EQ(QuicVersionMax(), connection_.version());

  // Expect that the lowest version is selected.
  // Ensure the lowest supported version is less than the max, unless they're
  // the same.
  ParsedQuicVersionVector lowest_version_vector;
  lowest_version_vector.push_back(QuicVersionMin());
  EXPECT_TRUE(connection_.SelectMutualVersion(lowest_version_vector));
  EXPECT_EQ(QuicVersionMin(), connection_.version());

  // Shouldn't be able to find a mutually supported version.
  ParsedQuicVersionVector unsupported_version;
  unsupported_version.push_back(UnsupportedQuicVersion());
  EXPECT_FALSE(connection_.SelectMutualVersion(unsupported_version));
}

TEST_P(QuicConnectionTest, ConnectionCloseWhenWritable) {
  EXPECT_FALSE(writer_->IsWriteBlocked());

  // Send a packet.
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_EQ(1u, writer_->packets_write_attempts());

  TriggerConnectionClose();
  EXPECT_LE(2u, writer_->packets_write_attempts());
}

TEST_P(QuicConnectionTest, ConnectionCloseGettingWriteBlocked) {
  BlockOnNextWrite();
  TriggerConnectionClose();
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_TRUE(writer_->IsWriteBlocked());
}

TEST_P(QuicConnectionTest, ConnectionCloseWhenWriteBlocked) {
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_TRUE(writer_->IsWriteBlocked());
  TriggerConnectionClose();
  EXPECT_EQ(1u, writer_->packets_write_attempts());
}

TEST_P(QuicConnectionTest, OnPacketSentDebugVisitor) {
  PathProbeTestInit(Perspective::IS_CLIENT);
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);

  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(1, "foo", 0, NO_FIN);

  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(1);
  connection_.SendConnectivityProbingPacket(writer_.get(),
                                            connection_.peer_address());
}

TEST_P(QuicConnectionTest, OnPacketHeaderDebugVisitor) {
  QuicPacketHeader header;
  header.packet_number = QuicPacketNumber(1);
  if (GetParam().version.HasIetfInvariantHeader()) {
    header.form = IETF_QUIC_LONG_HEADER_PACKET;
  }

  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);
  EXPECT_CALL(debug_visitor, OnPacketHeader(Ref(header), _, _)).Times(1);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_)).Times(1);
  EXPECT_CALL(debug_visitor, OnSuccessfulVersionNegotiation(_)).Times(1);
  connection_.OnPacketHeader(header);
}

TEST_P(QuicConnectionTest, Pacing) {
  TestConnection server(connection_id_, kPeerAddress, kSelfAddress,
                        helper_.get(), alarm_factory_.get(), writer_.get(),
                        Perspective::IS_SERVER, version());
  TestConnection client(connection_id_, kSelfAddress, kPeerAddress,
                        helper_.get(), alarm_factory_.get(), writer_.get(),
                        Perspective::IS_CLIENT, version());
  EXPECT_FALSE(QuicSentPacketManagerPeer::UsingPacing(
      static_cast<const QuicSentPacketManager*>(
          &client.sent_packet_manager())));
  EXPECT_FALSE(QuicSentPacketManagerPeer::UsingPacing(
      static_cast<const QuicSentPacketManager*>(
          &server.sent_packet_manager())));
}

TEST_P(QuicConnectionTest, WindowUpdateInstigateAcks) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  // Send a WINDOW_UPDATE frame.
  QuicWindowUpdateFrame* window_update = new QuicWindowUpdateFrame();
  window_update->stream_id = 3;
  window_update->max_data = 1234;
  EXPECT_CALL(visitor_, OnWindowUpdateFrame(_));
  ProcessFramePacket(QuicFrame(window_update));

  // Ensure that this has caused the ACK alarm to be set.
  EXPECT_TRUE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, BlockedFrameInstigateAcks) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  // Send a BLOCKED frame.
  QuicBlockedFrame* blocked = new QuicBlockedFrame();
  blocked->stream_id = 3;
  EXPECT_CALL(visitor_, OnBlockedFrame(_));
  ProcessFramePacket(QuicFrame(blocked));

  // Ensure that this has caused the ACK alarm to be set.
  EXPECT_TRUE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, ReevaluateTimeUntilSendOnAck) {
  // Enable pacing.
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);

  // Send two packets.  One packet is not sufficient because if it gets acked,
  // there will be no packets in flight after that and the pacer will always
  // allow the next packet in that situation.
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, NO_FIN);
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "bar",
      3, NO_FIN);
  connection_.OnCanWrite();

  // Schedule the next packet for a few milliseconds in future.
  QuicSentPacketManagerPeer::DisablePacerBursts(manager_);
  QuicTime scheduled_pacing_time =
      clock_.Now() + QuicTime::Delta::FromMilliseconds(5);
  QuicSentPacketManagerPeer::SetNextPacedPacketTime(manager_,
                                                    scheduled_pacing_time);

  // Send a packet and have it be blocked by congestion control.
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(false));
  connection_.SendStreamDataWithString(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "baz",
      6, NO_FIN);
  EXPECT_FALSE(connection_.GetSendAlarm()->IsSet());

  // Process an ack and the send alarm will be set to the new 5ms delay.
  QuicAckFrame ack = InitAckFrame(1);
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  ProcessAckPacket(&ack);
  size_t padding_frame_count = writer_->padding_frames().size();
  EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_TRUE(connection_.GetSendAlarm()->IsSet());
  EXPECT_EQ(scheduled_pacing_time, connection_.GetSendAlarm()->deadline());
  writer_->Reset();
}

TEST_P(QuicConnectionTest, SendAcksImmediately) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacket(1);
  CongestionBlockWrites();
  SendAckPacketToPeer();
}

TEST_P(QuicConnectionTest, SendPingImmediately) {
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);

  CongestionBlockWrites();
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(1);
  EXPECT_CALL(debug_visitor, OnPingSent()).Times(1);
  connection_.SendControlFrame(QuicFrame(QuicPingFrame(1)));
  EXPECT_FALSE(connection_.HasQueuedData());
}

TEST_P(QuicConnectionTest, SendBlockedImmediately) {
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(1);
  EXPECT_EQ(0u, connection_.GetStats().blocked_frames_sent);
  connection_.SendControlFrame(QuicFrame(new QuicBlockedFrame(1, 3)));
  EXPECT_EQ(1u, connection_.GetStats().blocked_frames_sent);
  EXPECT_FALSE(connection_.HasQueuedData());
}

TEST_P(QuicConnectionTest, FailedToSendBlockedFrames) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);
  QuicBlockedFrame blocked(1, 3);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(0);
  EXPECT_EQ(0u, connection_.GetStats().blocked_frames_sent);
  connection_.SendControlFrame(QuicFrame(&blocked));
  EXPECT_EQ(0u, connection_.GetStats().blocked_frames_sent);
  EXPECT_FALSE(connection_.HasQueuedData());
}

TEST_P(QuicConnectionTest, SendingUnencryptedStreamDataFails) {
  // EXPECT_QUIC_BUG tests are expensive so only run one instance of them.
  if (!IsDefaultTestConfiguration()) {
    return;
  }

  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  struct iovec iov;
  MakeIOVector("", &iov);
  EXPECT_QUIC_BUG(connection_.SaveAndSendStreamData(3, &iov, 1, 0, 0, FIN),
                  "Cannot send stream data with level: ENCRYPTION_INITIAL");
  EXPECT_FALSE(connection_.connected());
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA));
}

TEST_P(QuicConnectionTest, SetRetransmissionAlarmForCryptoPacket) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendCryptoStreamData();

  // Verify retransmission timer is correctly set after crypto packet has been
  // sent.
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  QuicTime retransmission_time =
      QuicConnectionPeer::GetSentPacketManager(&connection_)
          ->GetRetransmissionTime();
  EXPECT_NE(retransmission_time, clock_.ApproximateNow());
  EXPECT_EQ(retransmission_time,
            connection_.GetRetransmissionAlarm()->deadline());

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.GetRetransmissionAlarm()->Fire();
}

// Includes regression test for b/69979024.
TEST_P(QuicConnectionTest, PathDegradingDetectionForNonCryptoPackets) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_FALSE(connection_.IsPathDegrading());

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  for (int i = 0; i < 2; ++i) {
    // Send a packet. Now there's a retransmittable packet on the wire, so the
    // path degrading detection should be set.
    connection_.SendStreamDataWithString(
        GetNthClientInitiatedStreamId(1, connection_.transport_version()), data,
        offset, NO_FIN);
    offset += data_size;
    EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
    // Check the deadline of the path degrading detection.
    QuicTime::Delta delay =
        QuicConnectionPeer::GetSentPacketManager(&connection_)
            ->GetPathDegradingDelay();
    EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                         clock_.ApproximateNow());

    // Send a second packet. The path degrading detection's deadline should
    // remain the same.
    // Regression test for b/69979024.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
    QuicTime prev_deadline =
        connection_.GetBlackholeDetectorAlarm()->deadline();
    connection_.SendStreamDataWithString(
        GetNthClientInitiatedStreamId(1, connection_.transport_version()), data,
        offset, NO_FIN);
    offset += data_size;
    EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
    EXPECT_EQ(prev_deadline,
              connection_.GetBlackholeDetectorAlarm()->deadline());

    // Now receive an ACK of the first packet. This should advance the path
    // degrading detection's deadline since forward progress has been made.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
    if (i == 0) {
      EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
    }
    EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
    QuicAckFrame frame = InitAckFrame(
        {{QuicPacketNumber(1u + 2u * i), QuicPacketNumber(2u + 2u * i)}});
    ProcessAckPacket(&frame);
    EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
    // Check the deadline of the path degrading detection.
    delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
                ->GetPathDegradingDelay();
    EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                         clock_.ApproximateNow());

    if (i == 0) {
      // Now receive an ACK of the second packet. Since there are no more
      // retransmittable packets on the wire, this should cancel the path
      // degrading detection.
      clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
      EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
      frame = InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
      ProcessAckPacket(&frame);
      EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
    } else {
      // Advance time to the path degrading alarm's deadline and simulate
      // firing the alarm.
      clock_.AdvanceTime(delay);
      EXPECT_CALL(visitor_, OnPathDegrading());
      connection_.PathDegradingTimeout();
      EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
    }
  }
  EXPECT_TRUE(connection_.IsPathDegrading());
}

TEST_P(QuicConnectionTest, RetransmittableOnWireSetsPingAlarm) {
  const QuicTime::Delta retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(50);
  connection_.set_initial_retransmittable_on_wire_timeout(
      retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  // Send a packet.
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  // Now there's a retransmittable packet on the wire, so the path degrading
  // alarm should be set.
  // The retransmittable-on-wire alarm should not be set.
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  QuicTime::Delta delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
                              ->GetPathDegradingDelay();
  EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                       clock_.ApproximateNow());
  ASSERT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  QuicTime::Delta ping_delay = QuicTime::Delta::FromSeconds(kPingTimeoutSecs);
  EXPECT_EQ(ping_delay,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now receive an ACK of the packet.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(&frame);
  // No more retransmittable packets on the wire, so the path degrading alarm
  // should be cancelled, and the ping alarm should be set to the
  // retransmittable_on_wire_timeout.
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Simulate firing the ping alarm and sending a PING.
  clock_.AdvanceTime(retransmittable_on_wire_timeout);
  connection_.GetPingAlarm()->Fire();

  // Now there's a retransmittable packet (PING) on the wire, so the path
  // degrading alarm should be set.
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
              ->GetPathDegradingDelay();
  EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                       clock_.ApproximateNow());
}

TEST_P(QuicConnectionTest, ServerRetransmittableOnWire) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  SetQuicReloadableFlag(quic_enable_server_on_wire_ping, true);

  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kSRWP);
  config.SetInitialReceivedConnectionOptions(connection_options);
  connection_.SetFromConfig(config);

  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));

  ProcessPacket(1);

  ASSERT_TRUE(connection_.GetPingAlarm()->IsSet());
  QuicTime::Delta ping_delay = QuicTime::Delta::FromMilliseconds(200);
  EXPECT_EQ(ping_delay,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(10));
  connection_.SendStreamDataWithString(2, "foo", 0, NO_FIN);
  // Verify PING alarm gets cancelled.
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());

  // Now receive an ACK of the packet.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(100));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(2, &frame);
  // Verify PING alarm gets scheduled.
  ASSERT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(ping_delay,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
}

// This test verifies that the connection marks path as degrading and does not
// spin timer to detect path degrading when a new packet is sent on the
// degraded path.
TEST_P(QuicConnectionTest, NoPathDegradingDetectionIfPathIsDegrading) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_FALSE(connection_.IsPathDegrading());

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  // Send the first packet. Now there's a retransmittable packet on the wire, so
  // the path degrading alarm should be set.
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  // Check the deadline of the path degrading detection.
  QuicTime::Delta delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
                              ->GetPathDegradingDelay();
  EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                       clock_.ApproximateNow());

  // Send a second packet. The path degrading detection's deadline should remain
  // the same.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicTime prev_deadline = connection_.GetBlackholeDetectorAlarm()->deadline();
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  EXPECT_EQ(prev_deadline, connection_.GetBlackholeDetectorAlarm()->deadline());

  // Now receive an ACK of the first packet. This should advance the path
  // degrading detection's deadline since forward progress has been made.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1u), QuicPacketNumber(2u)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  // Check the deadline of the path degrading alarm.
  delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
              ->GetPathDegradingDelay();
  EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                       clock_.ApproximateNow());

  // Advance time to the path degrading detection's deadline and simulate
  // firing the path degrading detection. This path will be considered as
  // degrading.
  clock_.AdvanceTime(delay);
  EXPECT_CALL(visitor_, OnPathDegrading()).Times(1);
  connection_.PathDegradingTimeout();
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_TRUE(connection_.IsPathDegrading());

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  // Send a third packet. The path degrading detection is no longer set but path
  // should still be marked as degrading.
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_TRUE(connection_.IsPathDegrading());
}

// This test verifies that the connection unmarks path as degrarding and spins
// the timer to detect future path degrading when forward progress is made
// after path has been marked degrading.
TEST_P(QuicConnectionTest, UnmarkPathDegradingOnForwardProgress) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_FALSE(connection_.IsPathDegrading());

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  // Send the first packet. Now there's a retransmittable packet on the wire, so
  // the path degrading alarm should be set.
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  // Check the deadline of the path degrading alarm.
  QuicTime::Delta delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
                              ->GetPathDegradingDelay();
  EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                       clock_.ApproximateNow());

  // Send a second packet. The path degrading alarm's deadline should remain
  // the same.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicTime prev_deadline = connection_.GetBlackholeDetectorAlarm()->deadline();
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  EXPECT_EQ(prev_deadline, connection_.GetBlackholeDetectorAlarm()->deadline());

  // Now receive an ACK of the first packet. This should advance the path
  // degrading alarm's deadline since forward progress has been made.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1u), QuicPacketNumber(2u)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  // Check the deadline of the path degrading alarm.
  delay = QuicConnectionPeer::GetSentPacketManager(&connection_)
              ->GetPathDegradingDelay();
  EXPECT_EQ(delay, connection_.GetBlackholeDetectorAlarm()->deadline() -
                       clock_.ApproximateNow());

  // Advance time to the path degrading alarm's deadline and simulate
  // firing the alarm.
  clock_.AdvanceTime(delay);
  EXPECT_CALL(visitor_, OnPathDegrading()).Times(1);
  connection_.PathDegradingTimeout();
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_TRUE(connection_.IsPathDegrading());

  // Send a third packet. The path degrading alarm is no longer set but path
  // should still be marked as degrading.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
  EXPECT_TRUE(connection_.IsPathDegrading());

  // Now receive an ACK of the second packet. This should unmark the path as
  // degrading. And will set a timer to detect new path degrading.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(visitor_, OnForwardProgressMadeAfterPathDegrading()).Times(1);
  frame = InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
  ProcessAckPacket(&frame);
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
}

TEST_P(QuicConnectionTest, NoPathDegradingOnServer) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());

  // Send data.
  const char data[] = "data";
  connection_.SendStreamDataWithString(1, data, 0, NO_FIN);
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());

  // Ack data.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1u), QuicPacketNumber(2u)}});
  ProcessAckPacket(&frame);
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
}

TEST_P(QuicConnectionTest, NoPathDegradingAfterSendingAck) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacket(1);
  SendAckPacketToPeer();
  EXPECT_FALSE(connection_.sent_packet_manager().unacked_packets().empty());
  EXPECT_FALSE(connection_.sent_packet_manager().HasInFlightPackets());
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());
}

TEST_P(QuicConnectionTest, MultipleCallsToCloseConnection) {
  // Verifies that multiple calls to CloseConnection do not
  // result in multiple attempts to close the connection - it will be marked as
  // disconnected after the first call.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(1);
  connection_.CloseConnection(QUIC_NO_ERROR, "no reason",
                              ConnectionCloseBehavior::SILENT_CLOSE);
  connection_.CloseConnection(QUIC_NO_ERROR, "no reason",
                              ConnectionCloseBehavior::SILENT_CLOSE);
}

TEST_P(QuicConnectionTest, ServerReceivesChloOnNonCryptoStream) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  CryptoHandshakeMessage message;
  CryptoFramer framer;
  message.set_tag(kCHLO);
  std::unique_ptr<QuicData> data = framer.ConstructHandshakeMessage(message);
  frame1_.stream_id = 10;
  frame1_.data_buffer = data->data();
  frame1_.data_length = data->length();

  if (version().handshake_protocol == PROTOCOL_TLS1_3) {
    EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  }
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  ForceProcessFramePacket(QuicFrame(frame1_));
  TestConnectionCloseQuicErrorCode(QUIC_MAYBE_CORRUPTED_MEMORY);
}

TEST_P(QuicConnectionTest, ClientReceivesRejOnNonCryptoStream) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  CryptoHandshakeMessage message;
  CryptoFramer framer;
  message.set_tag(kREJ);
  std::unique_ptr<QuicData> data = framer.ConstructHandshakeMessage(message);
  frame1_.stream_id = 10;
  frame1_.data_buffer = data->data();
  frame1_.data_length = data->length();

  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  ForceProcessFramePacket(QuicFrame(frame1_));
  TestConnectionCloseQuicErrorCode(QUIC_MAYBE_CORRUPTED_MEMORY);
}

TEST_P(QuicConnectionTest, CloseConnectionOnPacketTooLarge) {
  SimulateNextPacketTooLarge();
  // A connection close packet is sent
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(1);
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  TestConnectionCloseQuicErrorCode(QUIC_PACKET_WRITE_ERROR);
}

TEST_P(QuicConnectionTest, AlwaysGetPacketTooLarge) {
  // Test even we always get packet too large, we do not infinitely try to send
  // close packet.
  AlwaysGetPacketTooLarge();
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(1);
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  TestConnectionCloseQuicErrorCode(QUIC_PACKET_WRITE_ERROR);
}

TEST_P(QuicConnectionTest, CloseConnectionOnQueuedWriteError) {
  // Regression test for crbug.com/979507.
  //
  // If we get a write error when writing queued packets, we should attempt to
  // send a connection close packet, but if sending that fails, it shouldn't get
  // queued.

  // Queue a packet to write.
  BlockOnNextWrite();
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Configure writer to always fail.
  AlwaysGetPacketTooLarge();

  // Expect that we attempt to close the connection exactly once.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(1);

  // Unblock the writes and actually send.
  writer_->SetWritable();
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());

  TestConnectionCloseQuicErrorCode(QUIC_PACKET_WRITE_ERROR);
}

// Verify that if connection has no outstanding data, it notifies the send
// algorithm after the write.
TEST_P(QuicConnectionTest, SendDataAndBecomeApplicationLimited) {
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(1);
  {
    InSequence seq;
    EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillRepeatedly(Return(true));
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    EXPECT_CALL(visitor_, WillingAndAbleToWrite())
        .WillRepeatedly(Return(false));
  }

  connection_.SendStreamData3();
}

// Verify that the connection does not become app-limited if there is
// outstanding data to send after the write.
TEST_P(QuicConnectionTest, NotBecomeApplicationLimitedIfMoreDataAvailable) {
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(0);
  {
    InSequence seq;
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillRepeatedly(Return(true));
  }

  connection_.SendStreamData3();
}

// Verify that the connection does not become app-limited after blocked write
// even if there is outstanding data to send after the write.
TEST_P(QuicConnectionTest, NotBecomeApplicationLimitedDueToWriteBlock) {
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(0);
  EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillRepeatedly(Return(true));
  BlockOnNextWrite();

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamData3();

  // Now unblock the writer, become congestion control blocked,
  // and ensure we become app-limited after writing.
  writer_->SetWritable();
  CongestionBlockWrites();
  EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillRepeatedly(Return(false));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(1);
  connection_.OnCanWrite();
}

// Test the mode in which the link is filled up with probing retransmissions if
// the connection becomes application-limited.
TEST_P(QuicConnectionTest, SendDataWhenApplicationLimited) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, ShouldSendProbingPacket())
      .WillRepeatedly(Return(true));
  {
    InSequence seq;
    EXPECT_CALL(visitor_, WillingAndAbleToWrite()).WillRepeatedly(Return(true));
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    EXPECT_CALL(visitor_, WillingAndAbleToWrite())
        .WillRepeatedly(Return(false));
  }
  EXPECT_CALL(visitor_, SendProbingData()).WillRepeatedly([this] {
    return connection_.sent_packet_manager().MaybeRetransmitOldestPacket(
        PROBING_RETRANSMISSION);
  });
  // Fix congestion window to be 20,000 bytes.
  EXPECT_CALL(*send_algorithm_, CanSend(Ge(20000u)))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*send_algorithm_, CanSend(Lt(20000u)))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(0);
  ASSERT_EQ(0u, connection_.GetStats().packets_sent);
  connection_.set_fill_up_link_during_probing(true);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();
  connection_.SendStreamData3();

  // We expect a lot of packets from a 20 kbyte window.
  EXPECT_GT(connection_.GetStats().packets_sent, 10u);
  // Ensure that the packets are padded.
  QuicByteCount average_packet_size =
      connection_.GetStats().bytes_sent / connection_.GetStats().packets_sent;
  EXPECT_GT(average_packet_size, 1000u);

  // Acknowledge all packets sent, except for the last one.
  QuicAckFrame ack = InitAckFrame(
      connection_.sent_packet_manager().GetLargestSentPacket() - 1);
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));

  // Ensure that since we no longer have retransmittable bytes in flight, this
  // will not cause any responses to be sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(1);
  ProcessAckPacket(&ack);
}

TEST_P(QuicConnectionTest, DoNotForceSendingAckOnPacketTooLarge) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  // Send an ack by simulating delayed ack alarm firing.
  ProcessPacket(1);
  EXPECT_TRUE(connection_.HasPendingAcks());
  connection_.GetAckAlarm()->Fire();
  // Simulate data packet causes write error.
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  SimulateNextPacketTooLarge();
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  EXPECT_EQ(1u, writer_->connection_close_frames().size());
  // Ack frame is not bundled in connection close packet.
  EXPECT_TRUE(writer_->ack_frames().empty());
  if (writer_->padding_frames().empty()) {
    EXPECT_EQ(1u, writer_->frame_count());
  } else {
    EXPECT_EQ(2u, writer_->frame_count());
  }

  TestConnectionCloseQuicErrorCode(QUIC_PACKET_WRITE_ERROR);
}

TEST_P(QuicConnectionTest, CloseConnectionAllLevels) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }

  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  const QuicErrorCode kQuicErrorCode = QUIC_INTERNAL_ERROR;
  connection_.CloseConnection(
      kQuicErrorCode, "Some random error message",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);

  EXPECT_EQ(2u, QuicConnectionPeer::GetNumEncryptionLevels(&connection_));

  TestConnectionCloseQuicErrorCode(kQuicErrorCode);
  EXPECT_EQ(1u, writer_->connection_close_frames().size());

  if (!connection_.version().CanSendCoalescedPackets()) {
    // Each connection close packet should be sent in distinct UDP packets.
    EXPECT_EQ(QuicConnectionPeer::GetNumEncryptionLevels(&connection_),
              writer_->connection_close_packets());
    EXPECT_EQ(QuicConnectionPeer::GetNumEncryptionLevels(&connection_),
              writer_->packets_write_attempts());
    return;
  }

  // A single UDP packet should be sent with multiple connection close packets
  // coalesced together.
  EXPECT_EQ(1u, writer_->packets_write_attempts());

  // Only the first packet has been processed yet.
  EXPECT_EQ(1u, writer_->connection_close_packets());

  // ProcessPacket resets the visitor and frees the coalesced packet.
  ASSERT_TRUE(writer_->coalesced_packet() != nullptr);
  auto packet = writer_->coalesced_packet()->Clone();
  writer_->framer()->ProcessPacket(*packet);
  EXPECT_EQ(1u, writer_->connection_close_packets());
  ASSERT_TRUE(writer_->coalesced_packet() == nullptr);
}

TEST_P(QuicConnectionTest, CloseConnectionOneLevel) {
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }

  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  const QuicErrorCode kQuicErrorCode = QUIC_INTERNAL_ERROR;
  connection_.CloseConnection(
      kQuicErrorCode, "Some random error message",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);

  EXPECT_EQ(2u, QuicConnectionPeer::GetNumEncryptionLevels(&connection_));

  TestConnectionCloseQuicErrorCode(kQuicErrorCode);
  EXPECT_EQ(1u, writer_->connection_close_frames().size());
  EXPECT_EQ(1u, writer_->connection_close_packets());
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  ASSERT_TRUE(writer_->coalesced_packet() == nullptr);
}

TEST_P(QuicConnectionTest, DoNotPadServerInitialConnectionClose) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);

  if (version().handshake_protocol == PROTOCOL_TLS1_3) {
    EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  }
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  const QuicErrorCode kQuicErrorCode = QUIC_INTERNAL_ERROR;
  connection_.CloseConnection(
      kQuicErrorCode, "Some random error message",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);

  EXPECT_EQ(2u, QuicConnectionPeer::GetNumEncryptionLevels(&connection_));

  TestConnectionCloseQuicErrorCode(kQuicErrorCode);
  EXPECT_EQ(1u, writer_->connection_close_frames().size());
  EXPECT_TRUE(writer_->padding_frames().empty());
  EXPECT_EQ(ENCRYPTION_INITIAL, writer_->framer()->last_decrypted_level());
}

// Regression test for b/63620844.
TEST_P(QuicConnectionTest, FailedToWriteHandshakePacket) {
  SimulateNextPacketTooLarge();
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(1);

  connection_.SendCryptoStreamData();
  TestConnectionCloseQuicErrorCode(QUIC_PACKET_WRITE_ERROR);
}

TEST_P(QuicConnectionTest, MaxPacingRate) {
  EXPECT_EQ(0, connection_.MaxPacingRate().ToBytesPerSecond());
  connection_.SetMaxPacingRate(QuicBandwidth::FromBytesPerSecond(100));
  EXPECT_EQ(100, connection_.MaxPacingRate().ToBytesPerSecond());
}

TEST_P(QuicConnectionTest, ClientAlwaysSendConnectionId) {
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(3, "foo", 0, NO_FIN);
  EXPECT_EQ(CONNECTION_ID_PRESENT,
            writer_->last_packet_header().destination_connection_id_included);

  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  QuicConfigPeer::SetReceivedBytesForConnectionId(&config, 0);
  connection_.SetFromConfig(config);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(3, "bar", 3, NO_FIN);
  // Verify connection id is still sent in the packet.
  EXPECT_EQ(CONNECTION_ID_PRESENT,
            writer_->last_packet_header().destination_connection_id_included);
}

TEST_P(QuicConnectionTest, SendProbingRetransmissions) {
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);

  const QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "bar", 3, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "test", 6, NO_FIN, &last_packet);

  const QuicByteCount old_bytes_in_flight =
      connection_.sent_packet_manager().GetBytesInFlight();

  // Allow 9 probing retransmissions to be sent.
  {
    InSequence seq;
    EXPECT_CALL(*send_algorithm_, CanSend(_))
        .Times(9 * 2)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  }
  // Expect them retransmitted in cyclic order (foo, bar, test, foo, bar...).
  QuicPacketCount sent_count = 0;

  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _))
      .WillRepeatedly(Invoke(
          [this, &sent_count](QuicPacketNumber, QuicPacketLength, bool,
                              TransmissionType, EncryptionLevel,
                              const QuicFrames&, const QuicFrames&, QuicTime) {
            ASSERT_EQ(1u, writer_->stream_frames().size());
            if (connection_.version().CanSendCoalescedPackets()) {
              // There is a delay of sending coalesced packet, so (6, 0, 3, 6,
              // 0...).
              EXPECT_EQ(3 * ((sent_count + 2) % 3),
                        writer_->stream_frames()[0]->offset);
            } else {
              // Identify the frames by stream offset (0, 3, 6, 0, 3...).
              EXPECT_EQ(3 * (sent_count % 3),
                        writer_->stream_frames()[0]->offset);
            }
            sent_count++;
          }));

  EXPECT_CALL(*send_algorithm_, ShouldSendProbingPacket())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(visitor_, SendProbingData()).WillRepeatedly([this] {
    return connection_.sent_packet_manager().MaybeRetransmitOldestPacket(
        PROBING_RETRANSMISSION);
  });

  connection_.SendProbingRetransmissions();

  // Ensure that the in-flight has increased.
  const QuicByteCount new_bytes_in_flight =
      connection_.sent_packet_manager().GetBytesInFlight();
  EXPECT_GT(new_bytes_in_flight, old_bytes_in_flight);
}

// Ensure that SendProbingRetransmissions() does not retransmit anything when
// there are no outstanding packets.
TEST_P(QuicConnectionTest,
       SendProbingRetransmissionsFailsWhenNothingToRetransmit) {
  ASSERT_TRUE(connection_.sent_packet_manager().unacked_packets().empty());

  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);
  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*send_algorithm_, ShouldSendProbingPacket())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(visitor_, SendProbingData()).WillRepeatedly([this] {
    return connection_.sent_packet_manager().MaybeRetransmitOldestPacket(
        PROBING_RETRANSMISSION);
  });

  connection_.SendProbingRetransmissions();
}

TEST_P(QuicConnectionTest, PingAfterLastRetransmittablePacketAcked) {
  const QuicTime::Delta retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(50);
  connection_.set_initial_retransmittable_on_wire_timeout(
      retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  // Advance 5ms, send a retransmittable packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  QuicTime::Delta ping_delay = QuicTime::Delta::FromSeconds(kPingTimeoutSecs);
  EXPECT_EQ(ping_delay,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Advance 5ms, send a second retransmittable packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());

  // Now receive an ACK of the first packet. This should not set the
  // retransmittable-on-wire alarm since packet 2 is still on the wire.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  // The ping alarm has a 1 second granularity, and the clock has been advanced
  // 10ms since it was originally set.
  EXPECT_EQ(ping_delay - QuicTime::Delta::FromMilliseconds(10),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now receive an ACK of the second packet. This should set the
  // retransmittable-on-wire alarm now that no retransmittable packets are on
  // the wire.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  frame = InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now receive a duplicate ACK of the second packet. This should not update
  // the ping alarm.
  QuicTime prev_deadline = connection_.GetPingAlarm()->deadline();
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  frame = InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(prev_deadline, connection_.GetPingAlarm()->deadline());

  // Now receive a non-ACK packet.  This should not update the ping alarm.
  prev_deadline = connection_.GetPingAlarm()->deadline();
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  ProcessPacket(4);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(prev_deadline, connection_.GetPingAlarm()->deadline());

  // Simulate the alarm firing and check that a PING is sent.
  connection_.GetPingAlarm()->Fire();
  size_t padding_frame_count = writer_->padding_frames().size();
  if (GetParam().no_stop_waiting) {
    EXPECT_EQ(padding_frame_count + 2u, writer_->frame_count());
  } else {
    EXPECT_EQ(padding_frame_count + 3u, writer_->frame_count());
  }
  ASSERT_EQ(1u, writer_->ping_frames().size());
}

TEST_P(QuicConnectionTest, NoPingIfRetransmittablePacketSent) {
  const QuicTime::Delta retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(50);
  connection_.set_initial_retransmittable_on_wire_timeout(
      retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));

  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;

  // Advance 5ms, send a retransmittable packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  QuicTime::Delta ping_delay = QuicTime::Delta::FromSeconds(kPingTimeoutSecs);
  EXPECT_EQ(ping_delay,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now receive an ACK of the first packet. This should set the
  // retransmittable-on-wire alarm now that no retransmittable packets are on
  // the wire.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Before the alarm fires, send another retransmittable packet. This should
  // cancel the retransmittable-on-wire alarm since now there's a
  // retransmittable packet on the wire.
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());

  // Now receive an ACK of the second packet. This should set the
  // retransmittable-on-wire alarm now that no retransmittable packets are on
  // the wire.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  frame = InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Simulate the alarm firing and check that a PING is sent.
  writer_->Reset();
  connection_.GetPingAlarm()->Fire();
  size_t padding_frame_count = writer_->padding_frames().size();
  if (GetParam().no_stop_waiting) {
    // Do not ACK acks.
    EXPECT_EQ(padding_frame_count + 1u, writer_->frame_count());
  } else {
    EXPECT_EQ(padding_frame_count + 3u, writer_->frame_count());
  }
  ASSERT_EQ(1u, writer_->ping_frames().size());
}

// When there is no stream data received but are open streams, send the
// first few consecutive pings with aggressive retransmittable-on-wire
// timeout. Exponentially back off the retransmittable-on-wire ping timeout
// afterwards until it exceeds the default ping timeout.
TEST_P(QuicConnectionTest, BackOffRetransmittableOnWireTimeout) {
  int max_aggressive_retransmittable_on_wire_ping_count = 5;
  SetQuicFlag(FLAGS_quic_max_aggressive_retransmittable_on_wire_ping_count,
              max_aggressive_retransmittable_on_wire_ping_count);
  const QuicTime::Delta initial_retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(200);
  connection_.set_initial_retransmittable_on_wire_timeout(
      initial_retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));

  const char data[] = "data";
  // Advance 5ms, send a retransmittable data packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  connection_.SendStreamDataWithString(1, data, 0, NO_FIN);
  EXPECT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());

  // Verify that the first few consecutive retransmittable on wire pings are
  // sent with aggressive timeout.
  for (int i = 0; i <= max_aggressive_retransmittable_on_wire_ping_count; i++) {
    // Receive an ACK of the previous packet. This should set the ping alarm
    // with the initial retransmittable-on-wire timeout.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
    QuicPacketNumber ack_num = creator_->packet_number();
    QuicAckFrame frame = InitAckFrame(
        {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
    ProcessAckPacket(&frame);
    EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
    EXPECT_EQ(initial_retransmittable_on_wire_timeout,
              connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
    // Simulate the alarm firing and check that a PING is sent.
    writer_->Reset();
    clock_.AdvanceTime(initial_retransmittable_on_wire_timeout);
    connection_.GetPingAlarm()->Fire();
  }

  QuicTime::Delta retransmittable_on_wire_timeout =
      initial_retransmittable_on_wire_timeout;

  // Verify subsequent pings are sent with timeout that is exponentially backed
  // off.
  while (retransmittable_on_wire_timeout * 2 < connection_.ping_timeout()) {
    // Receive an ACK for the previous PING. This should set the
    // ping alarm with backed off retransmittable-on-wire timeout.
    retransmittable_on_wire_timeout = retransmittable_on_wire_timeout * 2;
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
    QuicPacketNumber ack_num = creator_->packet_number();
    QuicAckFrame frame = InitAckFrame(
        {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
    ProcessAckPacket(&frame);
    EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
    EXPECT_EQ(retransmittable_on_wire_timeout,
              connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

    // Simulate the alarm firing and check that a PING is sent.
    writer_->Reset();
    clock_.AdvanceTime(retransmittable_on_wire_timeout);
    connection_.GetPingAlarm()->Fire();
  }

  // The ping alarm is set with default ping timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Receive an ACK for the previous PING. The ping alarm is set with an
  // earlier deadline.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicPacketNumber ack_num = creator_->packet_number();
  QuicAckFrame frame = InitAckFrame(
      {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout() - QuicTime::Delta::FromMilliseconds(5),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
}

// This test verify that the count of consecutive aggressive pings is reset
// when new data is received. And it also verifies the connection resets
// the exponential back-off of the retransmittable-on-wire ping timeout
// after receiving new stream data.
TEST_P(QuicConnectionTest, ResetBackOffRetransmitableOnWireTimeout) {
  int max_aggressive_retransmittable_on_wire_ping_count = 3;
  SetQuicFlag(FLAGS_quic_max_aggressive_retransmittable_on_wire_ping_count, 3);
  const QuicTime::Delta initial_retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(200);
  connection_.set_initial_retransmittable_on_wire_timeout(
      initial_retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());

  const char data[] = "data";
  // Advance 5ms, send a retransmittable data packet to the peer.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  connection_.SendStreamDataWithString(1, data, 0, NO_FIN);
  EXPECT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Receive an ACK of the first packet. This should set the ping alarm with
  // initial retransmittable-on-wire timeout since there is no retransmittable
  // packet on the wire.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(2)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(initial_retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Simulate the alarm firing and check that a PING is sent.
  writer_->Reset();
  clock_.AdvanceTime(initial_retransmittable_on_wire_timeout);
  connection_.GetPingAlarm()->Fire();

  // Receive an ACK for the previous PING. Ping alarm will be set with
  // aggressive timeout.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicPacketNumber ack_num = creator_->packet_number();
  frame = InitAckFrame(
      {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(initial_retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Process a data packet.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacket(peer_creator_.packet_number() + 1);
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_,
                                         peer_creator_.packet_number() + 1);
  EXPECT_EQ(initial_retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Verify the count of consecutive aggressive pings is reset.
  for (int i = 0; i < max_aggressive_retransmittable_on_wire_ping_count; i++) {
    // Receive an ACK of the previous packet. This should set the ping alarm
    // with the initial retransmittable-on-wire timeout.
    QuicPacketNumber ack_num = creator_->packet_number();
    QuicAckFrame frame = InitAckFrame(
        {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
    ProcessAckPacket(&frame);
    EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
    EXPECT_EQ(initial_retransmittable_on_wire_timeout,
              connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
    // Simulate the alarm firing and check that a PING is sent.
    writer_->Reset();
    clock_.AdvanceTime(initial_retransmittable_on_wire_timeout);
    connection_.GetPingAlarm()->Fire();
    // Advance 5ms to receive next packet.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  }

  // Receive another ACK for the previous PING. This should set the
  // ping alarm with backed off retransmittable-on-wire timeout.
  ack_num = creator_->packet_number();
  frame = InitAckFrame(
      {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(initial_retransmittable_on_wire_timeout * 2,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  writer_->Reset();
  clock_.AdvanceTime(2 * initial_retransmittable_on_wire_timeout);
  connection_.GetPingAlarm()->Fire();

  // Process another data packet and a new ACK packet. The ping alarm is set
  // with aggressive ping timeout again.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  ProcessDataPacket(peer_creator_.packet_number() + 1);
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_,
                                         peer_creator_.packet_number() + 1);
  ack_num = creator_->packet_number();
  frame = InitAckFrame(
      {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(initial_retransmittable_on_wire_timeout,
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
}

// Make sure that we never send more retransmissible on the wire pings than
// the limit in FLAGS_quic_max_retransmittable_on_wire_ping_count.
TEST_P(QuicConnectionTest, RetransmittableOnWirePingLimit) {
  static constexpr int kMaxRetransmittableOnWirePingCount = 3;
  SetQuicFlag(FLAGS_quic_max_retransmittable_on_wire_ping_count,
              kMaxRetransmittableOnWirePingCount);
  static constexpr QuicTime::Delta initial_retransmittable_on_wire_timeout =
      QuicTime::Delta::FromMilliseconds(200);
  static constexpr QuicTime::Delta short_delay =
      QuicTime::Delta::FromMilliseconds(5);
  ASSERT_LT(short_delay * 10, initial_retransmittable_on_wire_timeout);
  connection_.set_initial_retransmittable_on_wire_timeout(
      initial_retransmittable_on_wire_timeout);

  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));

  const char data[] = "data";
  // Advance 5ms, send a retransmittable data packet to the peer.
  clock_.AdvanceTime(short_delay);
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  connection_.SendStreamDataWithString(1, data, 0, NO_FIN);
  EXPECT_TRUE(connection_.sent_packet_manager().HasInFlightPackets());
  // The ping alarm is set for the ping timeout, not the shorter
  // retransmittable_on_wire_timeout.
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _))
      .Times(AnyNumber());

  // Verify that the first few consecutive retransmittable on wire pings are
  // sent with aggressive timeout.
  for (int i = 0; i <= kMaxRetransmittableOnWirePingCount; i++) {
    // Receive an ACK of the previous packet. This should set the ping alarm
    // with the initial retransmittable-on-wire timeout.
    clock_.AdvanceTime(short_delay);
    QuicPacketNumber ack_num = creator_->packet_number();
    QuicAckFrame frame = InitAckFrame(
        {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
    ProcessAckPacket(&frame);
    EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
    EXPECT_EQ(initial_retransmittable_on_wire_timeout,
              connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
    // Simulate the alarm firing and check that a PING is sent.
    writer_->Reset();
    clock_.AdvanceTime(initial_retransmittable_on_wire_timeout);
    connection_.GetPingAlarm()->Fire();
  }

  // Receive an ACK of the previous packet. This should set the ping alarm
  // but this time with the default ping timeout.
  QuicPacketNumber ack_num = creator_->packet_number();
  QuicAckFrame frame = InitAckFrame(
      {{QuicPacketNumber(ack_num), QuicPacketNumber(ack_num + 1)}});
  ProcessAckPacket(&frame);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_EQ(connection_.ping_timeout(),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());
}

TEST_P(QuicConnectionTest, ValidStatelessResetToken) {
  const QuicUint128 kTestToken = 1010101;
  const QuicUint128 kWrongTestToken = 1010100;
  QuicConfig config;
  // No token has been received.
  EXPECT_FALSE(connection_.IsValidStatelessResetToken(kTestToken));

  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _)).Times(2);
  // Token is different from received token.
  QuicConfigPeer::SetReceivedStatelessResetToken(&config, kTestToken);
  connection_.SetFromConfig(config);
  EXPECT_FALSE(connection_.IsValidStatelessResetToken(kWrongTestToken));

  QuicConfigPeer::SetReceivedStatelessResetToken(&config, kTestToken);
  connection_.SetFromConfig(config);
  EXPECT_TRUE(connection_.IsValidStatelessResetToken(kTestToken));
}

TEST_P(QuicConnectionTest, WriteBlockedWithInvalidAck) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);
  BlockOnNextWrite();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendStreamDataWithString(5, "foo", 0, FIN);
  // This causes connection to be closed because packet 1 has not been sent yet.
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  ProcessAckPacket(1, &frame);
  EXPECT_EQ(0, connection_close_frame_count_);
}

TEST_P(QuicConnectionTest, SendMessage) {
  if (!VersionSupportsMessageFrames(connection_.transport_version())) {
    return;
  }
  if (connection_.version().UsesTls()) {
    QuicConfig config;
    QuicConfigPeer::SetReceivedMaxDatagramFrameSize(
        &config, kMaxAcceptedDatagramFrameSize);
    EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
    connection_.SetFromConfig(config);
  }
  std::string message(connection_.GetCurrentLargestMessagePayload() * 2, 'a');
  absl::string_view message_data(message);
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendStreamData3();
    // Send a message which cannot fit into current open packet, and 2 packets
    // get sent, one contains stream frame, and the other only contains the
    // message frame.
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
    EXPECT_EQ(MESSAGE_STATUS_SUCCESS,
              connection_.SendMessage(
                  1,
                  MakeSpan(connection_.helper()->GetStreamSendBufferAllocator(),
                           absl::string_view(
                               message_data.data(),
                               connection_.GetCurrentLargestMessagePayload()),
                           &storage),
                  false));
  }
  // Fail to send a message if connection is congestion control blocked.
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillOnce(Return(false));
  EXPECT_EQ(MESSAGE_STATUS_BLOCKED,
            connection_.SendMessage(
                2,
                MakeSpan(connection_.helper()->GetStreamSendBufferAllocator(),
                         "message", &storage),
                false));

  // Always fail to send a message which cannot fit into one packet.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  EXPECT_EQ(MESSAGE_STATUS_TOO_LARGE,
            connection_.SendMessage(
                3,
                MakeSpan(connection_.helper()->GetStreamSendBufferAllocator(),
                         absl::string_view(
                             message_data.data(),
                             connection_.GetCurrentLargestMessagePayload() + 1),
                         &storage),
                false));
}

TEST_P(QuicConnectionTest, GetCurrentLargestMessagePayload) {
  if (!connection_.version().SupportsMessageFrames()) {
    return;
  }
  // Force use of this encrypter to simplify test expectations by making sure
  // that the encryption overhead is constant across versions.
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x00));
  QuicPacketLength expected_largest_payload = 1319;
  if (connection_.version().SendsVariableLengthPacketNumberInLongHeader()) {
    expected_largest_payload += 3;
  }
  if (connection_.version().HasLongHeaderLengths()) {
    expected_largest_payload -= 2;
  }
  if (connection_.version().HasLengthPrefixedConnectionIds()) {
    expected_largest_payload -= 1;
  }
  if (connection_.version().UsesTls()) {
    // QUIC+TLS disallows DATAGRAM/MESSAGE frames before the handshake.
    EXPECT_EQ(connection_.GetCurrentLargestMessagePayload(), 0);
    QuicConfig config;
    QuicConfigPeer::SetReceivedMaxDatagramFrameSize(
        &config, kMaxAcceptedDatagramFrameSize);
    EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
    connection_.SetFromConfig(config);
    // Verify the value post-handshake.
    EXPECT_EQ(connection_.GetCurrentLargestMessagePayload(),
              expected_largest_payload);
  } else {
    EXPECT_EQ(connection_.GetCurrentLargestMessagePayload(),
              expected_largest_payload);
  }
}

TEST_P(QuicConnectionTest, GetGuaranteedLargestMessagePayload) {
  if (!connection_.version().SupportsMessageFrames()) {
    return;
  }
  // Force use of this encrypter to simplify test expectations by making sure
  // that the encryption overhead is constant across versions.
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x00));
  QuicPacketLength expected_largest_payload = 1319;
  if (connection_.version().HasLongHeaderLengths()) {
    expected_largest_payload -= 2;
  }
  if (connection_.version().HasLengthPrefixedConnectionIds()) {
    expected_largest_payload -= 1;
  }
  if (connection_.version().UsesTls()) {
    // QUIC+TLS disallows DATAGRAM/MESSAGE frames before the handshake.
    EXPECT_EQ(connection_.GetGuaranteedLargestMessagePayload(), 0);
    QuicConfig config;
    QuicConfigPeer::SetReceivedMaxDatagramFrameSize(
        &config, kMaxAcceptedDatagramFrameSize);
    EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
    connection_.SetFromConfig(config);
    // Verify the value post-handshake.
    EXPECT_EQ(connection_.GetGuaranteedLargestMessagePayload(),
              expected_largest_payload);
  } else {
    EXPECT_EQ(connection_.GetGuaranteedLargestMessagePayload(),
              expected_largest_payload);
  }
}

TEST_P(QuicConnectionTest, LimitedLargestMessagePayload) {
  if (!connection_.version().SupportsMessageFrames() ||
      !connection_.version().UsesTls()) {
    return;
  }
  constexpr QuicPacketLength kFrameSizeLimit = 1000;
  constexpr QuicPacketLength kPayloadSizeLimit =
      kFrameSizeLimit - kQuicFrameTypeSize;
  // QUIC+TLS disallows DATAGRAM/MESSAGE frames before the handshake.
  EXPECT_EQ(connection_.GetCurrentLargestMessagePayload(), 0);
  EXPECT_EQ(connection_.GetGuaranteedLargestMessagePayload(), 0);
  QuicConfig config;
  QuicConfigPeer::SetReceivedMaxDatagramFrameSize(&config, kFrameSizeLimit);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  // Verify the value post-handshake.
  EXPECT_EQ(connection_.GetCurrentLargestMessagePayload(), kPayloadSizeLimit);
  EXPECT_EQ(connection_.GetGuaranteedLargestMessagePayload(),
            kPayloadSizeLimit);
}

// Test to check that the path challenge/path response logic works
// correctly. This test is only for version-99
TEST_P(QuicConnectionTest, ServerResponseToPathChallenge) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version)) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);
  QuicConnectionPeer::SetAddressValidated(&connection_);
  // First check if the server can send probing packet.
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  // Create and send the probe request (PATH_CHALLENGE frame).
  // SendConnectivityProbingPacket ends up calling
  // TestPacketWriter::WritePacket() which in turns receives and parses the
  // packet by calling framer_.ProcessPacket() -- which in turn calls
  // SimpleQuicFramer::OnPathChallengeFrame(). SimpleQuicFramer saves
  // the packet in writer_->path_challenge_frames()
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendConnectivityProbingPacket(writer_.get(),
                                            connection_.peer_address());
  // Save the random contents of the challenge for later comparison to the
  // response.
  ASSERT_GE(writer_->path_challenge_frames().size(), 1u);
  QuicPathFrameBuffer challenge_data =
      writer_->path_challenge_frames().front().data_buffer;

  // Normally, QuicConnection::OnPathChallengeFrame and OnPaddingFrame would be
  // called and it will perform actions to ensure that the rest of the protocol
  // is performed.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_TRUE(connection_.OnPathChallengeFrame(
      writer_->path_challenge_frames().front()));
  EXPECT_TRUE(connection_.OnPaddingFrame(writer_->padding_frames().front()));
  if (!connection_.send_path_response()) {
    connection_.SendConnectivityProbingResponsePacket(
        connection_.peer_address());
  }
  creator_->FlushCurrentPacket();

  // The final check is to ensure that the random data in the response matches
  // the random data from the challenge.
  EXPECT_EQ(1u, writer_->path_response_frames().size());
  EXPECT_EQ(0, memcmp(&challenge_data,
                      &(writer_->path_response_frames().front().data_buffer),
                      sizeof(challenge_data)));
}

TEST_P(QuicConnectionTest, ClientResponseToPathChallengeOnDefaulSocket) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.send_path_response()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  // First check if the client can send probing packet.
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  // Create and send the probe request (PATH_CHALLENGE frame).
  // SendConnectivityProbingPacket ends up calling
  // TestPacketWriter::WritePacket() which in turns receives and parses the
  // packet by calling framer_.ProcessPacket() -- which in turn calls
  // SimpleQuicFramer::OnPathChallengeFrame(). SimpleQuicFramer saves
  // the packet in writer_->path_challenge_frames()
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendConnectivityProbingPacket(writer_.get(),
                                            connection_.peer_address());
  // Save the random contents of the challenge for later validation against the
  // response.
  ASSERT_GE(writer_->path_challenge_frames().size(), 1u);
  QuicPathFrameBuffer challenge_data =
      writer_->path_challenge_frames().front().data_buffer;

  // Normally, QuicConnection::OnPathChallengeFrame would be
  // called and it will perform actions to ensure that the rest of the protocol
  // is performed.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_TRUE(connection_.OnPathChallengeFrame(
      writer_->path_challenge_frames().front()));
  EXPECT_TRUE(connection_.OnPaddingFrame(writer_->padding_frames().front()));
  creator_->FlushCurrentPacket();

  // The final check is to ensure that the random data in the response matches
  // the random data from the challenge.
  EXPECT_EQ(1u, writer_->path_response_frames().size());
  EXPECT_EQ(0, memcmp(&challenge_data,
                      &(writer_->path_response_frames().front().data_buffer),
                      sizeof(challenge_data)));
}

TEST_P(QuicConnectionTest, ClientResponseToPathChallengeOnAlternativeSocket) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);

  QuicSocketAddress kNewSelfAddress(QuicIpAddress::Loopback6(), /*port=*/23456);
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(1u, new_writer.packets_write_attempts());
        EXPECT_EQ(1u, new_writer.path_challenge_frames().size());
        EXPECT_EQ(1u, new_writer.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress.host(),
                  new_writer.last_write_source_address());
      }));
  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));

  // Receiving a PATH_CHALLENGE on the alternative path. Response to this
  // PATH_CHALLENGE should be sent via the alternative writer.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(2u, new_writer.packets_write_attempts());
        EXPECT_EQ(1u, new_writer.path_response_frames().size());
        EXPECT_EQ(1u, new_writer.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress.host(),
                  new_writer.last_write_source_address());
      }));
  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));
  ProcessReceivedPacket(kNewSelfAddress, kPeerAddress, *received);

  QuicSocketAddress kNewerSelfAddress(QuicIpAddress::Loopback6(),
                                      /*port=*/34567);
  // Receiving a PATH_CHALLENGE on an unknown socket should be ignored.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0u);
  ProcessReceivedPacket(kNewerSelfAddress, kPeerAddress, *received);
}

TEST_P(QuicConnectionTest,
       RestartPathDegradingDetectionAfterMigrationWithProbe) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  PathProbeTestInit(Perspective::IS_CLIENT);

  // Send data and verify the path degrading detection is set.
  const char data[] = "data";
  size_t data_size = strlen(data);
  QuicStreamOffset offset = 0;
  connection_.SendStreamDataWithString(1, data, offset, NO_FIN);
  offset += data_size;

  // Verify the path degrading detection is in progress.
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
  EXPECT_FALSE(connection_.IsPathDegrading());
  QuicTime ddl = connection_.GetBlackholeDetectorAlarm()->deadline();

  // Simulate the firing of path degrading.
  clock_.AdvanceTime(ddl - clock_.ApproximateNow());
  EXPECT_CALL(visitor_, OnPathDegrading()).Times(1);
  connection_.PathDegradingTimeout();
  EXPECT_TRUE(connection_.IsPathDegrading());
  EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());

  if (!GetParam().version.HasIetfQuicFrames()) {
    // Simulate path degrading handling by sending a probe on an alternet path.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
    TestPacketWriter probing_writer(version(), &clock_, Perspective::IS_CLIENT);
    connection_.SendConnectivityProbingPacket(&probing_writer,
                                              connection_.peer_address());
    // Verify that path degrading detection is not reset.
    EXPECT_FALSE(connection_.PathDegradingDetectionInProgress());

    // Simulate successful path degrading handling by receiving probe response.
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(20));

    EXPECT_CALL(visitor_,
                OnPacketReceived(_, _, /*is_connectivity_probe=*/true))
        .Times(1);
  const QuicSocketAddress kNewSelfAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);

  std::unique_ptr<SerializedPacket> probing_packet = ConstructProbingPacket();
  std::unique_ptr<QuicReceivedPacket> received(ConstructReceivedPacket(
      QuicEncryptedPacket(probing_packet->encrypted_buffer,
                          probing_packet->encrypted_length),
      clock_.Now()));
  uint64_t num_probing_received =
      connection_.GetStats().num_connectivity_probing_received;
  ProcessReceivedPacket(kNewSelfAddress, kPeerAddress, *received);

  EXPECT_EQ(num_probing_received + 1,
            connection_.GetStats().num_connectivity_probing_received);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
  EXPECT_TRUE(connection_.IsPathDegrading());
  }

  // Verify new path degrading detection is activated.
  EXPECT_CALL(visitor_, OnForwardProgressMadeAfterPathDegrading()).Times(1);
  connection_.OnSuccessfulMigration(/*is_port_change*/ true);
  EXPECT_FALSE(connection_.IsPathDegrading());
  EXPECT_TRUE(connection_.PathDegradingDetectionInProgress());
}

TEST_P(QuicConnectionTest, ClientsResetCwndAfterConnectionMigration) {
  if (!GetParam().version.HasIetfQuicFrames()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  PathProbeTestInit(Perspective::IS_CLIENT);
  EXPECT_EQ(kSelfAddress, connection_.self_address());

  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  QuicTime::Delta default_init_rtt = rtt_stats->initial_rtt();
  rtt_stats->set_initial_rtt(default_init_rtt * 2);
  EXPECT_EQ(2 * default_init_rtt, rtt_stats->initial_rtt());

  QuicSentPacketManagerPeer::SetConsecutiveRtoCount(manager_, 1);
  EXPECT_EQ(1u, manager_->GetConsecutiveRtoCount());
  QuicSentPacketManagerPeer::SetConsecutiveTlpCount(manager_, 2);
  EXPECT_EQ(2u, manager_->GetConsecutiveTlpCount());
  const SendAlgorithmInterface* send_algorithm = manager_->GetSendAlgorithm();

  // Migrate to a new address with different IP.
  const QuicSocketAddress kNewSelfAddress =
      QuicSocketAddress(QuicIpAddress::Loopback4(), /*port=*/23456);
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  connection_.MigratePath(kNewSelfAddress, connection_.peer_address(),
                          &new_writer, false);
  EXPECT_EQ(default_init_rtt, manager_->GetRttStats()->initial_rtt());
  EXPECT_EQ(0u, manager_->GetConsecutiveRtoCount());
  EXPECT_EQ(0u, manager_->GetConsecutiveTlpCount());
  EXPECT_NE(send_algorithm, manager_->GetSendAlgorithm());
}

// Regression test for b/110259444
TEST_P(QuicConnectionTest, DoNotScheduleSpuriousAckAlarm) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AtLeast(1));
  writer_->SetWriteBlocked();

  ProcessPacket(1);
  // Verify ack alarm is set.
  EXPECT_TRUE(connection_.HasPendingAcks());
  // Fire the ack alarm, verify no packet is sent because the writer is blocked.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.GetAckAlarm()->Fire();

  writer_->SetWritable();
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessPacket(2);
  // Verify ack alarm is not set.
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, DisablePacingOffloadConnectionOptions) {
  EXPECT_FALSE(QuicConnectionPeer::SupportsReleaseTime(&connection_));
  writer_->set_supports_release_time(true);
  QuicConfig config;
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  EXPECT_TRUE(QuicConnectionPeer::SupportsReleaseTime(&connection_));

  QuicTagVector connection_options;
  connection_options.push_back(kNPCO);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  // Verify pacing offload is disabled.
  EXPECT_FALSE(QuicConnectionPeer::SupportsReleaseTime(&connection_));
}

// Regression test for b/110259444
// Get a path response without having issued a path challenge...
TEST_P(QuicConnectionTest, OrphanPathResponse) {
  QuicPathFrameBuffer data = {{0, 1, 2, 3, 4, 5, 6, 7}};

  QuicPathResponseFrame frame(99, data);
  EXPECT_TRUE(connection_.OnPathResponseFrame(frame));
  // If PATH_RESPONSE was accepted (payload matches the payload saved
  // in QuicConnection::transmitted_connectivity_probe_payload_) then
  // current_packet_content_ would be set to FIRST_FRAME_IS_PING.
  // Since this PATH_RESPONSE does not match, current_packet_content_
  // must not be FIRST_FRAME_IS_PING.
  EXPECT_NE(QuicConnection::FIRST_FRAME_IS_PING,
            QuicConnectionPeer::GetCurrentPacketContent(&connection_));
}

// Regression test for b/120791670
TEST_P(QuicConnectionTest, StopProcessingGQuicPacketInIetfQuicConnection) {
  // This test mimics a problematic scenario where a QUIC connection using a
  // modern version received a Q043 packet and processed it incorrectly.
  // We can remove this test once Q043 is deprecated.
  if (!version().HasIetfInvariantHeader()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);

  // Let connection process a Google QUIC packet.
  peer_framer_.set_version_for_tests(ParsedQuicVersion::Q043());
  std::unique_ptr<QuicPacket> packet(
      ConstructDataPacket(2, !kHasStopWaiting, ENCRYPTION_INITIAL));
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      peer_framer_.EncryptPayload(ENCRYPTION_INITIAL, QuicPacketNumber(2),
                                  *packet, buffer, kMaxOutgoingPacketSize);
  // Make sure no stream frame is processed.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(0);
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, encrypted_length, clock_.Now(), false));

  EXPECT_EQ(2u, connection_.GetStats().packets_received);
  EXPECT_EQ(1u, connection_.GetStats().packets_processed);
}

TEST_P(QuicConnectionTest, AcceptPacketNumberZero) {
  if (!VersionHasIetfQuicFrames(version().transport_version)) {
    return;
  }
  // Set first_sending_packet_number to be 0 to allow successfully processing
  // acks which ack packet number 0.
  QuicFramerPeer::SetFirstSendingPacketNumber(writer_->framer()->framer(), 0);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  ProcessPacket(0);
  EXPECT_EQ(QuicPacketNumber(0), LargestAcked(connection_.ack_frame()));
  EXPECT_EQ(1u, connection_.ack_frame().packets.NumIntervals());

  ProcessPacket(1);
  EXPECT_EQ(QuicPacketNumber(1), LargestAcked(connection_.ack_frame()));
  EXPECT_EQ(1u, connection_.ack_frame().packets.NumIntervals());

  ProcessPacket(2);
  EXPECT_EQ(QuicPacketNumber(2), LargestAcked(connection_.ack_frame()));
  EXPECT_EQ(1u, connection_.ack_frame().packets.NumIntervals());
}

TEST_P(QuicConnectionTest, MultiplePacketNumberSpacesBasicSending) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));

  connection_.SendCryptoStreamData();
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  QuicAckFrame frame1 = InitAckFrame(1);
  // Received ACK for packet 1.
  ProcessFramePacketAtLevel(30, QuicFrame(&frame1), ENCRYPTION_INITIAL);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(4);
  connection_.SendApplicationDataAtLevel(ENCRYPTION_ZERO_RTT, 5, "data", 0,
                                         NO_FIN);
  connection_.SendApplicationDataAtLevel(ENCRYPTION_ZERO_RTT, 5, "data", 4,
                                         NO_FIN);
  connection_.SendApplicationDataAtLevel(ENCRYPTION_FORWARD_SECURE, 5, "data",
                                         8, NO_FIN);
  connection_.SendApplicationDataAtLevel(ENCRYPTION_FORWARD_SECURE, 5, "data",
                                         12, FIN);
  // Received ACK for packets 2, 4, 5.
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  QuicAckFrame frame2 =
      InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)},
                    {QuicPacketNumber(4), QuicPacketNumber(6)}});
  // Make sure although the same packet number is used, but they are in
  // different packet number spaces.
  ProcessFramePacketAtLevel(30, QuicFrame(&frame2), ENCRYPTION_FORWARD_SECURE);
}

TEST_P(QuicConnectionTest, PeerAcksPacketsInWrongPacketNumberSpace) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x01));

  connection_.SendCryptoStreamData();
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  QuicAckFrame frame1 = InitAckFrame(1);
  // Received ACK for packet 1.
  ProcessFramePacketAtLevel(30, QuicFrame(&frame1), ENCRYPTION_INITIAL);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  connection_.SendApplicationDataAtLevel(ENCRYPTION_ZERO_RTT, 5, "data", 0,
                                         NO_FIN);
  connection_.SendApplicationDataAtLevel(ENCRYPTION_ZERO_RTT, 5, "data", 4,
                                         NO_FIN);

  // Received ACK for packets 2 and 3 in wrong packet number space.
  QuicAckFrame invalid_ack =
      InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(4)}});
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  ProcessFramePacketAtLevel(300, QuicFrame(&invalid_ack), ENCRYPTION_INITIAL);
  TestConnectionCloseQuicErrorCode(QUIC_INVALID_ACK_DATA);
}

TEST_P(QuicConnectionTest, MultiplePacketNumberSpacesBasicReceiving) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(0x02));
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x02));
  // Receives packet 1000 in application data.
  ProcessDataPacketAtLevel(1000, false, ENCRYPTION_ZERO_RTT);
  EXPECT_TRUE(connection_.HasPendingAcks());
  connection_.SendApplicationDataAtLevel(ENCRYPTION_ZERO_RTT, 5, "data", 0,
                                         NO_FIN);
  // Verify application data ACK gets bundled with outgoing data.
  EXPECT_EQ(2u, writer_->frame_count());
  // Make sure ACK alarm is still set because initial data is not ACKed.
  EXPECT_TRUE(connection_.HasPendingAcks());
  // Receive packet 1001 in application data.
  ProcessDataPacketAtLevel(1001, false, ENCRYPTION_ZERO_RTT);
  clock_.AdvanceTime(DefaultRetransmissionTime());
  // Simulates ACK alarm fires and verify two ACKs are flushed.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.GetAckAlarm()->Fire();
  EXPECT_FALSE(connection_.HasPendingAcks());
  // Receives more packets in application data.
  ProcessDataPacketAtLevel(1002, false, ENCRYPTION_ZERO_RTT);
  EXPECT_TRUE(connection_.HasPendingAcks());

  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0x02));
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  // Verify zero rtt and forward secure packets get acked in the same packet.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessDataPacket(1003);
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, CancelAckAlarmOnWriteBlocked) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());
  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(0x02));
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x02));
  // Receives packet 1000 in application data.
  ProcessDataPacketAtLevel(1000, false, ENCRYPTION_ZERO_RTT);
  EXPECT_TRUE(connection_.HasPendingAcks());

  writer_->SetWriteBlocked();
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AnyNumber());
  // Simulates ACK alarm fires and verify no ACK is flushed because of write
  // blocked.
  clock_.AdvanceTime(DefaultDelayedAckTime());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.GetAckAlarm()->Fire();
  // Verify ACK alarm is not set.
  EXPECT_FALSE(connection_.HasPendingAcks());

  writer_->SetWritable();
  // Verify 2 ACKs are sent when connection gets unblocked.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  connection_.OnCanWrite();
  EXPECT_FALSE(connection_.HasPendingAcks());
}

// Make sure a packet received with the right client connection ID is processed.
TEST_P(QuicConnectionTest, ValidClientConnectionId) {
  if (!framer_.version().SupportsClientConnectionIds()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  connection_.set_client_connection_id(TestConnectionId(0x33));
  QuicPacketHeader header = ConstructPacketHeader(1, ENCRYPTION_FORWARD_SECURE);
  header.destination_connection_id = TestConnectionId(0x33);
  header.destination_connection_id_included = CONNECTION_ID_PRESENT;
  header.source_connection_id_included = CONNECTION_ID_ABSENT;
  QuicFrames frames;
  QuicPingFrame ping_frame;
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(ping_frame));
  frames.push_back(QuicFrame(padding_frame));
  std::unique_ptr<QuicPacket> packet =
      BuildUnsizedDataPacket(&framer_, header, frames);
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length = peer_framer_.EncryptPayload(
      ENCRYPTION_FORWARD_SECURE, QuicPacketNumber(1), *packet, buffer,
      kMaxOutgoingPacketSize);
  QuicReceivedPacket received_packet(buffer, encrypted_length, clock_.Now(),
                                     false);
  EXPECT_EQ(0u, connection_.GetStats().packets_dropped);
  ProcessReceivedPacket(kSelfAddress, kPeerAddress, received_packet);
  EXPECT_EQ(0u, connection_.GetStats().packets_dropped);
}

// Make sure a packet received with a different client connection ID is dropped.
TEST_P(QuicConnectionTest, InvalidClientConnectionId) {
  if (!framer_.version().SupportsClientConnectionIds()) {
    return;
  }
  connection_.set_client_connection_id(TestConnectionId(0x33));
  QuicPacketHeader header = ConstructPacketHeader(1, ENCRYPTION_FORWARD_SECURE);
  header.destination_connection_id = TestConnectionId(0xbad);
  header.destination_connection_id_included = CONNECTION_ID_PRESENT;
  header.source_connection_id_included = CONNECTION_ID_ABSENT;
  QuicFrames frames;
  QuicPingFrame ping_frame;
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(ping_frame));
  frames.push_back(QuicFrame(padding_frame));
  std::unique_ptr<QuicPacket> packet =
      BuildUnsizedDataPacket(&framer_, header, frames);
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length = peer_framer_.EncryptPayload(
      ENCRYPTION_FORWARD_SECURE, QuicPacketNumber(1), *packet, buffer,
      kMaxOutgoingPacketSize);
  QuicReceivedPacket received_packet(buffer, encrypted_length, clock_.Now(),
                                     false);
  EXPECT_EQ(0u, connection_.GetStats().packets_dropped);
  ProcessReceivedPacket(kSelfAddress, kPeerAddress, received_packet);
  EXPECT_EQ(1u, connection_.GetStats().packets_dropped);
}

// Make sure the first packet received with a different client connection ID on
// the server is processed and it changes the client connection ID.
TEST_P(QuicConnectionTest, UpdateClientConnectionIdFromFirstPacket) {
  if (!framer_.version().SupportsClientConnectionIds()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  QuicPacketHeader header = ConstructPacketHeader(1, ENCRYPTION_INITIAL);
  header.source_connection_id = TestConnectionId(0x33);
  header.source_connection_id_included = CONNECTION_ID_PRESENT;
  QuicFrames frames;
  QuicPingFrame ping_frame;
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(ping_frame));
  frames.push_back(QuicFrame(padding_frame));
  std::unique_ptr<QuicPacket> packet =
      BuildUnsizedDataPacket(&framer_, header, frames);
  char buffer[kMaxOutgoingPacketSize];
  size_t encrypted_length =
      peer_framer_.EncryptPayload(ENCRYPTION_INITIAL, QuicPacketNumber(1),
                                  *packet, buffer, kMaxOutgoingPacketSize);
  QuicReceivedPacket received_packet(buffer, encrypted_length, clock_.Now(),
                                     false);
  EXPECT_EQ(0u, connection_.GetStats().packets_dropped);
  ProcessReceivedPacket(kSelfAddress, kPeerAddress, received_packet);
  EXPECT_EQ(0u, connection_.GetStats().packets_dropped);
  EXPECT_EQ(TestConnectionId(0x33), connection_.client_connection_id());
}

// Regression test for b/134416344.
TEST_P(QuicConnectionTest, CheckConnectedBeforeFlush) {
  // This test mimics a scenario where a connection processes 2 packets and the
  // 2nd packet contains connection close frame. When the 2nd flusher goes out
  // of scope, a delayed ACK is pending, and ACK alarm should not be scheduled
  // because connection is disconnected.
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());
  const QuicErrorCode kErrorCode = QUIC_INTERNAL_ERROR;
  std::unique_ptr<QuicConnectionCloseFrame> connection_close_frame(
      new QuicConnectionCloseFrame(connection_.transport_version(), kErrorCode,
                                   NO_IETF_QUIC_ERROR, "",
                                   /*transport_close_frame_type=*/0));

  // Received 2 packets.
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  }
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());
  ProcessFramePacketWithAddresses(QuicFrame(connection_close_frame.release()),
                                  kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  // Verify ack alarm is not set.
  EXPECT_FALSE(connection_.HasPendingAcks());
}

// Verify that a packet containing three coalesced packets is parsed correctly.
TEST_P(QuicConnectionTest, CoalescedPacket) {
  if (!QuicVersionHasLongHeaderLengths(connection_.transport_version())) {
    // Coalesced packets can only be encoded using long header lengths.
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_TRUE(connection_.connected());
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(3);
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(3);
  }

  uint64_t packet_numbers[3] = {1, 2, 3};
  EncryptionLevel encryption_levels[3] = {
      ENCRYPTION_INITIAL, ENCRYPTION_INITIAL, ENCRYPTION_FORWARD_SECURE};
  char buffer[kMaxOutgoingPacketSize] = {};
  size_t total_encrypted_length = 0;
  for (int i = 0; i < 3; i++) {
    QuicPacketHeader header =
        ConstructPacketHeader(packet_numbers[i], encryption_levels[i]);
    QuicFrames frames;
    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      frames.push_back(QuicFrame(&crypto_frame_));
    } else {
      frames.push_back(QuicFrame(frame1_));
    }
    std::unique_ptr<QuicPacket> packet = ConstructPacket(header, frames);
    peer_creator_.set_encryption_level(encryption_levels[i]);
    size_t encrypted_length = peer_framer_.EncryptPayload(
        encryption_levels[i], QuicPacketNumber(packet_numbers[i]), *packet,
        buffer + total_encrypted_length,
        sizeof(buffer) - total_encrypted_length);
    EXPECT_GT(encrypted_length, 0u);
    total_encrypted_length += encrypted_length;
  }
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, total_encrypted_length, clock_.Now(), false));
  if (connection_.GetSendAlarm()->IsSet()) {
    connection_.GetSendAlarm()->Fire();
  }

  EXPECT_TRUE(connection_.connected());
}

// Regression test for crbug.com/992831.
TEST_P(QuicConnectionTest, CoalescedPacketThatSavesFrames) {
  if (!QuicVersionHasLongHeaderLengths(connection_.transport_version())) {
    // Coalesced packets can only be encoded using long header lengths.
    return;
  }
  if (connection_.SupportsMultiplePacketNumberSpaces()) {
    // TODO(b/129151114) Enable this test with multiple packet number spaces.
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_TRUE(connection_.connected());
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_))
        .Times(3)
        .WillRepeatedly([this](const QuicCryptoFrame& /*frame*/) {
          // QuicFrame takes ownership of the QuicBlockedFrame.
          connection_.SendControlFrame(QuicFrame(new QuicBlockedFrame(1, 3)));
        });
  } else {
    EXPECT_CALL(visitor_, OnStreamFrame(_))
        .Times(3)
        .WillRepeatedly([this](const QuicStreamFrame& /*frame*/) {
          // QuicFrame takes ownership of the QuicBlockedFrame.
          connection_.SendControlFrame(QuicFrame(new QuicBlockedFrame(1, 3)));
        });
  }

  uint64_t packet_numbers[3] = {1, 2, 3};
  EncryptionLevel encryption_levels[3] = {
      ENCRYPTION_INITIAL, ENCRYPTION_INITIAL, ENCRYPTION_FORWARD_SECURE};
  char buffer[kMaxOutgoingPacketSize] = {};
  size_t total_encrypted_length = 0;
  for (int i = 0; i < 3; i++) {
    QuicPacketHeader header =
        ConstructPacketHeader(packet_numbers[i], encryption_levels[i]);
    QuicFrames frames;
    if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
      frames.push_back(QuicFrame(&crypto_frame_));
    } else {
      frames.push_back(QuicFrame(frame1_));
    }
    std::unique_ptr<QuicPacket> packet = ConstructPacket(header, frames);
    peer_creator_.set_encryption_level(encryption_levels[i]);
    size_t encrypted_length = peer_framer_.EncryptPayload(
        encryption_levels[i], QuicPacketNumber(packet_numbers[i]), *packet,
        buffer + total_encrypted_length,
        sizeof(buffer) - total_encrypted_length);
    EXPECT_GT(encrypted_length, 0u);
    total_encrypted_length += encrypted_length;
  }
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(buffer, total_encrypted_length, clock_.Now(), false));
  if (connection_.GetSendAlarm()->IsSet()) {
    connection_.GetSendAlarm()->Fire();
  }

  EXPECT_TRUE(connection_.connected());

  SendAckPacketToPeer();
}

// Regresstion test for b/138962304.
TEST_P(QuicConnectionTest, RtoAndWriteBlocked) {
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  QuicStreamId stream_id = 2;
  QuicPacketNumber last_data_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_data_packet);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Writer gets blocked.
  writer_->SetWriteBlocked();

  // Cancel the stream.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AtLeast(1));
  EXPECT_CALL(visitor_, WillingAndAbleToWrite())
      .WillRepeatedly(
          Invoke(&notifier_, &SimpleSessionNotifier::WillingToWrite));
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 3);

  // Retransmission timer fires in RTO mode.
  connection_.GetRetransmissionAlarm()->Fire();
  // Verify no packets get flushed when writer is blocked.
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

// Regresstion test for b/138962304.
TEST_P(QuicConnectionTest, TlpAndWriteBlocked) {
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  connection_.SetMaxTailLossProbes(1);

  QuicStreamId stream_id = 2;
  QuicPacketNumber last_data_packet;
  SendStreamDataToPeer(stream_id, "foo", 0, NO_FIN, &last_data_packet);
  SendStreamDataToPeer(4, "foo", 0, NO_FIN, &last_data_packet);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Writer gets blocked.
  writer_->SetWriteBlocked();

  // Cancel stream 2.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AtLeast(1));
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 3);

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  // Retransmission timer fires in TLP mode.
  connection_.GetRetransmissionAlarm()->Fire();
  // Verify one packets is forced flushed when writer is blocked.
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
}

// Regresstion test for b/139375344.
TEST_P(QuicConnectionTest, RtoForcesSendingPing) {
  if (connection_.PtoEnabled()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  connection_.SetMaxTailLossProbes(2);
  EXPECT_EQ(0u, connection_.GetStats().tlp_count);
  EXPECT_EQ(0u, connection_.GetStats().rto_count);

  SendStreamDataToPeer(2, "foo", 0, NO_FIN, nullptr);
  QuicTime retransmission_time =
      connection_.GetRetransmissionAlarm()->deadline();
  EXPECT_NE(QuicTime::Zero(), retransmission_time);
  // TLP fires.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(2), _, _));
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, connection_.GetStats().tlp_count);
  EXPECT_EQ(0u, connection_.GetStats().rto_count);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Packet 1 gets acked.
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  ProcessAckPacket(1, &frame);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  retransmission_time = connection_.GetRetransmissionAlarm()->deadline();

  // RTO fires, verify a PING packet gets sent because there is no data to send.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(3), _, _));
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, connection_.GetStats().tlp_count);
  EXPECT_EQ(1u, connection_.GetStats().rto_count);
  EXPECT_EQ(1u, writer_->ping_frames().size());
}

TEST_P(QuicConnectionTest, ProbeTimeout) {
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k2PTO);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foooooo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "foooooo", 7, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Reset stream.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  SendRstStream(stream_id, QUIC_ERROR_PROCESSING_STREAM, 3);

  // Fire the PTO and verify only the RST_STREAM is resent, not stream data.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(0u, writer_->stream_frames().size());
  EXPECT_EQ(1u, writer_->rst_stream_frames().size());
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, CloseConnectionAfter6ClientPTOs) {
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k1PTO);
  connection_options.push_back(k6PTO);
  config.SetConnectionOptionsToSend(connection_options);
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  }
  connection_.OnHandshakeComplete();
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);

  // Fire the retransmission alarm 5 times.
  for (int i = 0; i < 5; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.GetRetransmissionAlarm()->Fire();
    EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
    EXPECT_TRUE(connection_.connected());
  }
  EXPECT_CALL(visitor_, OnPathDegrading());
  connection_.PathDegradingTimeout();

  EXPECT_EQ(0u, connection_.sent_packet_manager().GetConsecutiveTlpCount());
  EXPECT_EQ(0u, connection_.sent_packet_manager().GetConsecutiveRtoCount());
  EXPECT_EQ(5u, connection_.sent_packet_manager().GetConsecutivePtoCount());
  // Closes connection on 6th PTO.
  // May send multiple connecction close packets with multiple PN spaces.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  ASSERT_TRUE(connection_.BlackholeDetectionInProgress());
  connection_.GetBlackholeDetectorAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_TOO_MANY_RTOS);
}

TEST_P(QuicConnectionTest, CloseConnectionAfter7ClientPTOs) {
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k2PTO);
  connection_options.push_back(k7PTO);
  config.SetConnectionOptionsToSend(connection_options);
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  }
  connection_.OnHandshakeComplete();
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);

  // Fire the retransmission alarm 6 times.
  for (int i = 0; i < 6; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    connection_.GetRetransmissionAlarm()->Fire();
    EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
    EXPECT_TRUE(connection_.connected());
  }
  EXPECT_CALL(visitor_, OnPathDegrading());
  connection_.PathDegradingTimeout();

  EXPECT_EQ(0u, connection_.sent_packet_manager().GetConsecutiveTlpCount());
  EXPECT_EQ(0u, connection_.sent_packet_manager().GetConsecutiveRtoCount());
  EXPECT_EQ(6u, connection_.sent_packet_manager().GetConsecutivePtoCount());
  // Closes connection on 7th PTO.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  ASSERT_TRUE(connection_.BlackholeDetectionInProgress());
  connection_.GetBlackholeDetectorAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_TOO_MANY_RTOS);
}

TEST_P(QuicConnectionTest, CloseConnectionAfter8ClientPTOs) {
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k2PTO);
  connection_options.push_back(k8PTO);
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  }
  connection_.OnHandshakeComplete();
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);

  // Fire the retransmission alarm 7 times.
  for (int i = 0; i < 7; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
    connection_.GetRetransmissionAlarm()->Fire();
    EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
    EXPECT_TRUE(connection_.connected());
  }
  EXPECT_CALL(visitor_, OnPathDegrading());
  connection_.PathDegradingTimeout();

  EXPECT_EQ(0u, connection_.sent_packet_manager().GetConsecutiveTlpCount());
  EXPECT_EQ(0u, connection_.sent_packet_manager().GetConsecutiveRtoCount());
  EXPECT_EQ(7u, connection_.sent_packet_manager().GetConsecutivePtoCount());
  // Closes connection on 8th PTO.
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  ASSERT_TRUE(connection_.BlackholeDetectionInProgress());
  connection_.GetBlackholeDetectorAlarm()->Fire();
  EXPECT_FALSE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_TOO_MANY_RTOS);
}

TEST_P(QuicConnectionTest, DeprecateHandshakeMode) {
  if (!connection_.version().SupportsAntiAmplificationLimit()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Send CHLO.
  connection_.SendCryptoStreamData();
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  QuicAckFrame frame1 = InitAckFrame(1);
  // Received ACK for packet 1.
  ProcessFramePacketAtLevel(1, QuicFrame(&frame1), ENCRYPTION_INITIAL);

  // Verify retransmission alarm is still set because handshake is not
  // confirmed although there is nothing in flight.
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_EQ(0u, connection_.GetStats().pto_count);
  EXPECT_EQ(0u, connection_.GetStats().crypto_retransmit_count);

  // PTO fires, verify a PING packet gets sent because there is no data to send.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _,
                           GetQuicReloadableFlag(quic_default_on_pto)
                               ? QuicPacketNumber(2)
                               : QuicPacketNumber(3),
                           _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, connection_.GetStats().pto_count);
  EXPECT_EQ(1u, connection_.GetStats().crypto_retransmit_count);
  EXPECT_EQ(1u, writer_->ping_frames().size());
}

TEST_P(QuicConnectionTest, AntiAmplificationLimit) {
  if (!connection_.version().SupportsAntiAmplificationLimit()) {
    return;
  }
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());

  set_perspective(Perspective::IS_SERVER);
  // Verify no data can be sent at the beginning because bytes received is 0.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo", 0);
  EXPECT_FALSE(connection_.CanWrite(HAS_RETRANSMITTABLE_DATA));
  EXPECT_FALSE(connection_.CanWrite(NO_RETRANSMITTABLE_DATA));
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Receives packet 1.
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);

  const size_t anti_amplification_factor =
      GetQuicFlag(FLAGS_quic_anti_amplification_factor);
  // Verify now packets can be sent.
  for (size_t i = 0; i < anti_amplification_factor; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendCryptoDataWithString("foo", i * 3);
    // Verify retransmission alarm is not set if throttled by anti-amplification
    // limit.
    EXPECT_EQ(i != anti_amplification_factor - 1,
              connection_.GetRetransmissionAlarm()->IsSet());
  }
  // Verify server is throttled by anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo", anti_amplification_factor * 3);

  // Receives packet 2.
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_INITIAL);
  // Verify more packets can be sent.
  for (size_t i = anti_amplification_factor; i < anti_amplification_factor * 2;
       ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendCryptoDataWithString("foo", i * 3);
  }
  // Verify server is throttled by anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo",
                                       2 * anti_amplification_factor * 3);

  ProcessPacket(3);
  // Verify anti-amplification limit is gone after address validation.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendStreamDataWithString(3, "first", i * 0, NO_FIN);
  }
}

TEST_P(QuicConnectionTest, 3AntiAmplificationLimit) {
  if (!connection_.version().SupportsAntiAmplificationLimit()) {
    return;
  }
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());

  set_perspective(Perspective::IS_SERVER);
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k3AFF);
  config.SetInitialReceivedConnectionOptions(connection_options);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(&config,
                                                         QuicConnectionId());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  // Verify no data can be sent at the beginning because bytes received is 0.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo", 0);
  EXPECT_FALSE(connection_.CanWrite(HAS_RETRANSMITTABLE_DATA));
  EXPECT_FALSE(connection_.CanWrite(NO_RETRANSMITTABLE_DATA));
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Receives packet 1.
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);

  const size_t anti_amplification_factor = 3;
  // Verify now packets can be sent.
  for (size_t i = 0; i < anti_amplification_factor; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendCryptoDataWithString("foo", i * 3);
    // Verify retransmission alarm is not set if throttled by anti-amplification
    // limit.
    EXPECT_EQ(i != anti_amplification_factor - 1,
              connection_.GetRetransmissionAlarm()->IsSet());
  }
  // Verify server is throttled by anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo", anti_amplification_factor * 3);

  // Receives packet 2.
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_INITIAL);
  // Verify more packets can be sent.
  for (size_t i = anti_amplification_factor; i < anti_amplification_factor * 2;
       ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendCryptoDataWithString("foo", i * 3);
  }
  // Verify server is throttled by anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo",
                                       2 * anti_amplification_factor * 3);

  ProcessPacket(3);
  // Verify anti-amplification limit is gone after address validation.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendStreamDataWithString(3, "first", i * 0, NO_FIN);
  }
}

TEST_P(QuicConnectionTest, 10AntiAmplificationLimit) {
  if (!connection_.version().SupportsAntiAmplificationLimit()) {
    return;
  }
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());

  set_perspective(Perspective::IS_SERVER);
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k10AF);
  config.SetInitialReceivedConnectionOptions(connection_options);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(&config,
                                                         QuicConnectionId());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  // Verify no data can be sent at the beginning because bytes received is 0.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo", 0);
  EXPECT_FALSE(connection_.CanWrite(HAS_RETRANSMITTABLE_DATA));
  EXPECT_FALSE(connection_.CanWrite(NO_RETRANSMITTABLE_DATA));
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Receives packet 1.
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);

  const size_t anti_amplification_factor = 10;
  // Verify now packets can be sent.
  for (size_t i = 0; i < anti_amplification_factor; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendCryptoDataWithString("foo", i * 3);
    // Verify retransmission alarm is not set if throttled by anti-amplification
    // limit.
    EXPECT_EQ(i != anti_amplification_factor - 1,
              connection_.GetRetransmissionAlarm()->IsSet());
  }
  // Verify server is throttled by anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo", anti_amplification_factor * 3);

  // Receives packet 2.
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_INITIAL);
  // Verify more packets can be sent.
  for (size_t i = anti_amplification_factor; i < anti_amplification_factor * 2;
       ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendCryptoDataWithString("foo", i * 3);
  }
  // Verify server is throttled by anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.SendCryptoDataWithString("foo",
                                       2 * anti_amplification_factor * 3);

  ProcessPacket(3);
  // Verify anti-amplification limit is gone after address validation.
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
    connection_.SendStreamDataWithString(3, "first", i * 0, NO_FIN);
  }
}

TEST_P(QuicConnectionTest, AckPendingWithAmplificationLimited) {
  if (!connection_.version().SupportsAntiAmplificationLimit()) {
    return;
  }
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(AnyNumber());
  set_perspective(Perspective::IS_SERVER);
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  // Receives packet 1.
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_TRUE(connection_.HasPendingAcks());
  // Send response in different encryption level and cause amplification factor
  // throttled.
  size_t i = 0;
  while (connection_.CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    connection_.SendCryptoDataWithString(std::string(1024, 'a'), i * 1024,
                                         ENCRYPTION_HANDSHAKE);
    ++i;
  }
  // Verify ACK is still pending.
  EXPECT_TRUE(connection_.HasPendingAcks());

  // Fire ACK alarm and verify ACK cannot be sent due to amplification factor.
  clock_.AdvanceTime(connection_.GetAckAlarm()->deadline() - clock_.Now());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.GetAckAlarm()->Fire();
  // Verify ACK alarm is cancelled.
  EXPECT_FALSE(connection_.HasPendingAcks());

  // Receives packet 2 and verify ACK gets flushed.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_INITIAL);
  EXPECT_FALSE(writer_->ack_frames().empty());
}

TEST_P(QuicConnectionTest, ConnectionCloseFrameType) {
  if (!VersionHasIetfQuicFrames(version().transport_version)) {
    // Test relevent only for IETF QUIC.
    return;
  }
  const QuicErrorCode kQuicErrorCode = IETF_QUIC_PROTOCOL_VIOLATION;
  // Use the (unknown) frame type of 9999 to avoid triggering any logic
  // which might be associated with the processing of a known frame type.
  const uint64_t kTransportCloseFrameType = 9999u;
  QuicFramerPeer::set_current_received_frame_type(
      QuicConnectionPeer::GetFramer(&connection_), kTransportCloseFrameType);
  // Do a transport connection close
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  connection_.CloseConnection(
      kQuicErrorCode, "Some random error message",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  const std::vector<QuicConnectionCloseFrame>& connection_close_frames =
      writer_->connection_close_frames();
  ASSERT_EQ(1u, connection_close_frames.size());
  EXPECT_EQ(IETF_QUIC_TRANSPORT_CONNECTION_CLOSE,
            connection_close_frames[0].close_type);
  EXPECT_EQ(kQuicErrorCode, connection_close_frames[0].quic_error_code);
  EXPECT_EQ(kTransportCloseFrameType,
            connection_close_frames[0].transport_close_frame_type);
}

// Regression test for b/137401387 and b/138962304.
TEST_P(QuicConnectionTest, RtoPacketAsTwo) {
  if (connection_.PtoEnabled()) {
    return;
  }
  connection_.SetMaxTailLossProbes(1);
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  std::string stream_data(3000, 's');
  // Send packets 1 - 66 and exhaust cwnd.
  for (size_t i = 0; i < 22; ++i) {
    // 3 packets for each stream, the first 2 are guaranteed to be full packets.
    SendStreamDataToPeer(i + 2, stream_data, 0, FIN, nullptr);
  }
  CongestionBlockWrites();

  // Fires TLP. Please note, this tail loss probe has 1 byte less stream data
  // compared to packet 1 because packet number length increases.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(67), _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  // Fires RTO. Please note, although packets 2 and 3 *should* be RTOed, but
  // packet 2 gets RTOed to two packets because packet number length increases.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(68), _, _));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(69), _, _));
  connection_.GetRetransmissionAlarm()->Fire();

  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  // Resets all streams except 2 and ack packets 1 and 2. Now, packet 3 is the
  // only one containing retransmittable frames.
  for (size_t i = 1; i < 22; ++i) {
    notifier_.OnStreamReset(i + 2, QUIC_STREAM_CANCELLED);
  }
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(3)}});
  ProcessAckPacket(1, &frame);
  CongestionUnblockWrites();

  // Fires TLP, verify a PING gets sent because packet 3 is marked
  // RTO_RETRANSMITTED.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(70), _, _));
  connection_.GetRetransmissionAlarm()->Fire();
}

TEST_P(QuicConnectionTest, PtoSkipsPacketNumber) {
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k1PTO);
  connection_options.push_back(kPTOS);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  QuicStreamId stream_id = 2;
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(stream_id, "foooooo", 0, NO_FIN, &last_packet);
  SendStreamDataToPeer(stream_id, "foooooo", 7, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(2), last_packet);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Fire PTO and verify the PTO retransmission skips one packet number.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(QuicPacketNumber(4), writer_->last_packet_header().packet_number);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, SendCoalescedPackets) {
  if (!connection_.version().CanSendCoalescedPackets()) {
    return;
  }
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);
  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(3);
  EXPECT_CALL(debug_visitor, OnCoalescedPacketSent(_, _)).Times(1);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    use_tagging_decrypter();
    connection_.SetEncrypter(ENCRYPTION_INITIAL,
                             std::make_unique<TaggingEncrypter>(0x01));
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
    connection_.SendCryptoDataWithString("foo", 0);
    // Verify this packet is on hold.
    EXPECT_EQ(0u, writer_->packets_write_attempts());

    connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                             std::make_unique<TaggingEncrypter>(0x02));
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
    connection_.SendCryptoDataWithString("bar", 3);
    EXPECT_EQ(0u, writer_->packets_write_attempts());

    connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                             std::make_unique<TaggingEncrypter>(0x03));
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    SendStreamDataToPeer(2, "baz", 3, NO_FIN, nullptr);
  }
  // Verify all 3 packets are coalesced in the same UDP datagram.
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_EQ(0x03030303u, writer_->final_bytes_of_last_packet());
  // Verify the packet is padded to full.
  EXPECT_EQ(connection_.max_packet_length(), writer_->last_packet_size());

  // Verify packet process.
  EXPECT_EQ(1u, writer_->crypto_frames().size());
  EXPECT_EQ(0u, writer_->stream_frames().size());
  // Verify there is coalesced packet.
  EXPECT_NE(nullptr, writer_->coalesced_packet());
}

TEST_P(QuicConnectionTest, LegacyVersionEncapsulation) {
  connection_.EnableLegacyVersionEncapsulation("test.example.org");

  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);
  EXPECT_CALL(debug_visitor, OnPacketSent(_, _, _, _, _, _, _, _)).Times(1);

  // Our TestPacketWriter normally parses the sent packet using the version
  // from the connection, so here we need to tell it to use the encapsulation
  // version, and reset the initial decrypter for that version.
  writer_->framer()->SetSupportedVersions(
      SupportedVersions(LegacyVersionForEncapsulation()));
  writer_->framer()->framer()->SetInitialObfuscators(
      connection_.connection_id());

  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendCryptoDataWithString("TEST_CRYPTO_DATA", /*offset=*/0);
  }

  EXPECT_EQ(1u, writer_->packets_write_attempts());
  // Verify that the packet is fully padded.
  EXPECT_EQ(connection_.max_packet_length(), writer_->last_packet_size());

  // Check that the connection stats show Legacy Version Encapsulation was used.
  EXPECT_GT(connection_.GetStats().sent_legacy_version_encapsulated_packets,
            0u);

  // Verify that the sent packet was in fact encapsulated, and check header.
  const QuicPacketHeader& encapsulated_header = writer_->last_packet_header();
  EXPECT_TRUE(encapsulated_header.version_flag);
  EXPECT_EQ(encapsulated_header.version, LegacyVersionForEncapsulation());
  EXPECT_EQ(encapsulated_header.destination_connection_id,
            connection_.connection_id());

  // Encapsulated packet should contain a stream frame for the crypto stream,
  // optionally padding, and nothing else.
  EXPECT_EQ(0u, writer_->crypto_frames().size());
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(writer_->frame_count(), writer_->framer()->padding_frames().size() +
                                        writer_->stream_frames().size());
}

TEST_P(QuicConnectionTest, ClientReceivedHandshakeDone) {
  if (!connection_.version().UsesTls()) {
    return;
  }
  EXPECT_CALL(visitor_, OnHandshakeDoneReceived());
  QuicFrames frames;
  frames.push_back(QuicFrame(QuicHandshakeDoneFrame()));
  frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
  ProcessFramesPacketAtLevel(1, frames, ENCRYPTION_FORWARD_SECURE);
}

TEST_P(QuicConnectionTest, ServerReceivedHandshakeDone) {
  if (!connection_.version().UsesTls()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  EXPECT_CALL(visitor_, OnHandshakeDoneReceived()).Times(0);
  if (version().handshake_protocol == PROTOCOL_TLS1_3) {
    EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  }
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(Invoke(this, &QuicConnectionTest::SaveConnectionCloseFrame));
  QuicFrames frames;
  frames.push_back(QuicFrame(QuicHandshakeDoneFrame()));
  frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
  ProcessFramesPacketAtLevel(1, frames, ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(1, connection_close_frame_count_);
  EXPECT_THAT(saved_connection_close_frame_.quic_error_code,
              IsError(IETF_QUIC_PROTOCOL_VIOLATION));
}

TEST_P(QuicConnectionTest, MultiplePacketNumberSpacePto) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  use_tagging_decrypter();
  // Send handshake packet.
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);
  EXPECT_EQ(0x02020202u, writer_->final_bytes_of_last_packet());

  // Send application data.
  connection_.SendApplicationDataAtLevel(ENCRYPTION_FORWARD_SECURE, 5, "data",
                                         0, NO_FIN);
  EXPECT_EQ(0x01010101u, writer_->final_bytes_of_last_packet());
  QuicTime retransmission_time =
      connection_.GetRetransmissionAlarm()->deadline();
  EXPECT_NE(QuicTime::Zero(), retransmission_time);

  // Retransmit handshake data.
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _,
                           GetQuicReloadableFlag(quic_default_on_pto)
                               ? QuicPacketNumber(3)
                               : QuicPacketNumber(4),
                           _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  // Verify 1-RTT packet gets coalesced with handshake retransmission.
  EXPECT_EQ(0x01010101u, writer_->final_bytes_of_last_packet());

  // Send application data.
  connection_.SendApplicationDataAtLevel(ENCRYPTION_FORWARD_SECURE, 5, "data",
                                         4, NO_FIN);
  EXPECT_EQ(0x01010101u, writer_->final_bytes_of_last_packet());
  retransmission_time = connection_.GetRetransmissionAlarm()->deadline();
  EXPECT_NE(QuicTime::Zero(), retransmission_time);

  // Retransmit handshake data again.
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  QuicPacketNumber handshake_retransmission =
      GetQuicReloadableFlag(quic_default_on_pto) ? QuicPacketNumber(5)
                                                 : QuicPacketNumber(7);
  handshake_retransmission += 1;
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, handshake_retransmission + 1, _, _));
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, handshake_retransmission, _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  // Verify 1-RTT packet gets coalesced with handshake retransmission.
  EXPECT_EQ(0x01010101u, writer_->final_bytes_of_last_packet());

  // Discard handshake key.
  connection_.OnHandshakeComplete();
  retransmission_time = connection_.GetRetransmissionAlarm()->deadline();
  EXPECT_NE(QuicTime::Zero(), retransmission_time);

  // Retransmit application data.
  clock_.AdvanceTime(retransmission_time - clock_.Now());
  QuicPacketNumber application_retransmission =
      GetQuicReloadableFlag(quic_default_on_pto) ? QuicPacketNumber(6)
                                                 : QuicPacketNumber(9);
  application_retransmission += 2;
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, application_retransmission, _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(0x01010101u, writer_->final_bytes_of_last_packet());
}

void QuicConnectionTest::TestClientRetryHandling(
    bool invalid_retry_tag,
    bool missing_original_id_in_config,
    bool wrong_original_id_in_config,
    bool missing_retry_id_in_config,
    bool wrong_retry_id_in_config) {
  if (invalid_retry_tag) {
    ASSERT_FALSE(missing_original_id_in_config);
    ASSERT_FALSE(wrong_original_id_in_config);
    ASSERT_FALSE(missing_retry_id_in_config);
    ASSERT_FALSE(wrong_retry_id_in_config);
  } else {
    ASSERT_FALSE(missing_original_id_in_config && wrong_original_id_in_config);
    ASSERT_FALSE(missing_retry_id_in_config && wrong_retry_id_in_config);
  }
  if (!version().UsesTls()) {
    return;
  }

  // These values come from draft-ietf-quic-tls Appendix A.4.
  char retry_packet_rfcv1[] = {
      0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0xf0, 0x67, 0xa5, 0x50, 0x2a,
      0x42, 0x62, 0xb5, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0x04, 0xa2, 0x65, 0xba,
      0x2e, 0xff, 0x4d, 0x82, 0x90, 0x58, 0xfb, 0x3f, 0x0f, 0x24, 0x96, 0xba};
  char retry_packet29[] = {
      0xff, 0xff, 0x00, 0x00, 0x1d, 0x00, 0x08, 0xf0, 0x67, 0xa5, 0x50, 0x2a,
      0x42, 0x62, 0xb5, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0xd1, 0x69, 0x26, 0xd8,
      0x1f, 0x6f, 0x9c, 0xa2, 0x95, 0x3a, 0x8a, 0xa4, 0x57, 0x5e, 0x1e, 0x49};

  char* retry_packet;
  size_t retry_packet_length;
  if (version() == ParsedQuicVersion::RFCv1()) {
    retry_packet = retry_packet_rfcv1;
    retry_packet_length = ABSL_ARRAYSIZE(retry_packet_rfcv1);
  } else if (version() == ParsedQuicVersion::Draft29()) {
    retry_packet = retry_packet29;
    retry_packet_length = ABSL_ARRAYSIZE(retry_packet29);
  } else {
    // TODO(dschinazi) generate retry packets for all versions once we have
    // server-side support for generating these programmatically.
    return;
  }

  char original_connection_id_bytes[] = {0x83, 0x94, 0xc8, 0xf0,
                                         0x3e, 0x51, 0x57, 0x08};
  char new_connection_id_bytes[] = {0xf0, 0x67, 0xa5, 0x50,
                                    0x2a, 0x42, 0x62, 0xb5};
  char retry_token_bytes[] = {0x74, 0x6f, 0x6b, 0x65, 0x6e};

  QuicConnectionId original_connection_id(
      original_connection_id_bytes,
      ABSL_ARRAYSIZE(original_connection_id_bytes));
  QuicConnectionId new_connection_id(new_connection_id_bytes,
                                     ABSL_ARRAYSIZE(new_connection_id_bytes));

  std::string retry_token(retry_token_bytes, ABSL_ARRAYSIZE(retry_token_bytes));

  if (invalid_retry_tag) {
    // Flip the last bit of the retry packet to prevent the integrity tag
    // from validating correctly.
    retry_packet[retry_packet_length - 1] ^= 1;
  }

  QuicConnectionId config_original_connection_id = original_connection_id;
  if (wrong_original_id_in_config) {
    // Flip the first bit of the connection ID.
    ASSERT_FALSE(config_original_connection_id.IsEmpty());
    config_original_connection_id.mutable_data()[0] ^= 0x80;
  }
  QuicConnectionId config_retry_source_connection_id = new_connection_id;
  if (wrong_retry_id_in_config) {
    // Flip the first bit of the connection ID.
    ASSERT_FALSE(config_retry_source_connection_id.IsEmpty());
    config_retry_source_connection_id.mutable_data()[0] ^= 0x80;
  }

  // Make sure the connection uses the connection ID from the test vectors,
  QuicConnectionPeer::SetServerConnectionId(&connection_,
                                            original_connection_id);
  // Make sure our fake framer has the new post-retry INITIAL keys so that any
  // retransmission triggered by retry can be decrypted.
  writer_->framer()->framer()->SetInitialObfuscators(new_connection_id);

  // Process the RETRY packet.
  connection_.ProcessUdpPacket(
      kSelfAddress, kPeerAddress,
      QuicReceivedPacket(retry_packet, retry_packet_length, clock_.Now()));

  if (invalid_retry_tag) {
    // Make sure we refuse to process a RETRY with invalid tag.
    EXPECT_FALSE(connection_.GetStats().retry_packet_processed);
    EXPECT_EQ(connection_.connection_id(), original_connection_id);
    EXPECT_TRUE(QuicPacketCreatorPeer::GetRetryToken(
                    QuicConnectionPeer::GetPacketCreator(&connection_))
                    .empty());
    return;
  }

  // Make sure we correctly parsed the RETRY.
  EXPECT_TRUE(connection_.GetStats().retry_packet_processed);
  EXPECT_EQ(connection_.connection_id(), new_connection_id);
  EXPECT_EQ(QuicPacketCreatorPeer::GetRetryToken(
                QuicConnectionPeer::GetPacketCreator(&connection_)),
            retry_token);

  // Test validating the original_connection_id from the config.
  QuicConfig received_config;
  QuicConfigPeer::SetNegotiated(&received_config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &received_config, connection_.connection_id());
    if (!missing_retry_id_in_config) {
      QuicConfigPeer::SetReceivedRetrySourceConnectionId(
          &received_config, config_retry_source_connection_id);
    }
  }
  if (!missing_original_id_in_config) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &received_config, config_original_connection_id);
  }

  if (missing_original_id_in_config || wrong_original_id_in_config ||
      missing_retry_id_in_config || wrong_retry_id_in_config) {
    EXPECT_CALL(visitor_,
                OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
        .Times(1);
  } else {
    EXPECT_CALL(visitor_,
                OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
        .Times(0);
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _)).Times(AnyNumber());
  connection_.SetFromConfig(received_config);
  if (missing_original_id_in_config || wrong_original_id_in_config ||
      missing_retry_id_in_config || wrong_retry_id_in_config) {
    ASSERT_FALSE(connection_.connected());
    TestConnectionCloseQuicErrorCode(IETF_QUIC_PROTOCOL_VIOLATION);
  } else {
    EXPECT_TRUE(connection_.connected());
  }
}

TEST_P(QuicConnectionTest, ClientParsesRetry) {
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);
}

TEST_P(QuicConnectionTest, ClientParsesRetryInvalidTag) {
  TestClientRetryHandling(/*invalid_retry_tag=*/true,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);
}

TEST_P(QuicConnectionTest, ClientParsesRetryMissingOriginalId) {
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/true,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);
}

TEST_P(QuicConnectionTest, ClientParsesRetryWrongOriginalId) {
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/true,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);
}

TEST_P(QuicConnectionTest, ClientParsesRetryMissingRetryId) {
  if (!connection_.version().UsesTls()) {
    // Versions that do not authenticate connection IDs never send the
    // retry_source_connection_id transport parameter.
    return;
  }
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/true,
                          /*wrong_retry_id_in_config=*/false);
}

TEST_P(QuicConnectionTest, ClientParsesRetryWrongRetryId) {
  if (!connection_.version().UsesTls()) {
    // Versions that do not authenticate connection IDs never send the
    // retry_source_connection_id transport parameter.
    return;
  }
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/true);
}

TEST_P(QuicConnectionTest, ClientRetransmitsInitialPacketsOnRetry) {
  if (!connection_.version().HasIetfQuicFrames()) {
    // TestClientRetryHandling() currently only supports IETF draft versions.
    return;
  }
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);

  connection_.SendCryptoStreamData();

  EXPECT_EQ(1u, writer_->packets_write_attempts());
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);

  // Verify that initial data is retransmitted immediately after receiving
  // RETRY.
  if (GetParam().ack_response == AckResponse::kImmediate) {
    EXPECT_EQ(2u, writer_->packets_write_attempts());
    EXPECT_EQ(1u, writer_->framer()->crypto_frames().size());
  }
}

TEST_P(QuicConnectionTest, NoInitialPacketsRetransmissionOnInvalidRetry) {
  if (!connection_.version().HasIetfQuicFrames()) {
    return;
  }
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);

  connection_.SendCryptoStreamData();

  EXPECT_EQ(1u, writer_->packets_write_attempts());
  TestClientRetryHandling(/*invalid_retry_tag=*/true,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);

  EXPECT_EQ(1u, writer_->packets_write_attempts());
}

TEST_P(QuicConnectionTest, ClientReceivesOriginalConnectionIdWithoutRetry) {
  if (!connection_.version().UsesTls()) {
    // QUIC+TLS is required to transmit connection ID transport parameters.
    return;
  }
  if (connection_.version().UsesTls()) {
    // Versions that authenticate connection IDs always send the
    // original_destination_connection_id transport parameter.
    return;
  }
  // Make sure that receiving the original_destination_connection_id transport
  // parameter fails the handshake when no RETRY packet was received before it.
  QuicConfig received_config;
  QuicConfigPeer::SetNegotiated(&received_config, true);
  QuicConfigPeer::SetReceivedOriginalConnectionId(&received_config,
                                                  TestConnectionId(0x12345));
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _)).Times(AnyNumber());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(1);
  connection_.SetFromConfig(received_config);
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(IETF_QUIC_PROTOCOL_VIOLATION);
}

TEST_P(QuicConnectionTest, ClientReceivesRetrySourceConnectionIdWithoutRetry) {
  if (!connection_.version().UsesTls()) {
    // Versions that do not authenticate connection IDs never send the
    // retry_source_connection_id transport parameter.
    return;
  }
  // Make sure that receiving the retry_source_connection_id transport parameter
  // fails the handshake when no RETRY packet was received before it.
  QuicConfig received_config;
  QuicConfigPeer::SetNegotiated(&received_config, true);
  QuicConfigPeer::SetReceivedRetrySourceConnectionId(&received_config,
                                                     TestConnectionId(0x12345));
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _)).Times(AnyNumber());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(1);
  connection_.SetFromConfig(received_config);
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(IETF_QUIC_PROTOCOL_VIOLATION);
}

// Regression test for http://crbug/1047977
TEST_P(QuicConnectionTest, MaxStreamsFrameCausesConnectionClose) {
  if (!VersionHasIetfQuicFrames(connection_.transport_version())) {
    return;
  }
  // Received frame causes connection close.
  EXPECT_CALL(visitor_, OnMaxStreamsFrame(_))
      .WillOnce(InvokeWithoutArgs([this]() {
        EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
        connection_.CloseConnection(
            QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES, "error",
            ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        return true;
      }));
  QuicFrames frames;
  frames.push_back(QuicFrame(QuicMaxStreamsFrame()));
  frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
  ProcessFramesPacketAtLevel(1, frames, ENCRYPTION_FORWARD_SECURE);
}

TEST_P(QuicConnectionTest, StreamsBlockedFrameCausesConnectionClose) {
  if (!VersionHasIetfQuicFrames(connection_.transport_version())) {
    return;
  }
  // Received frame causes connection close.
  EXPECT_CALL(visitor_, OnStreamsBlockedFrame(_))
      .WillOnce(InvokeWithoutArgs([this]() {
        EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
        connection_.CloseConnection(
            QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES, "error",
            ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        return true;
      }));
  QuicFrames frames;
  frames.push_back(
      QuicFrame(QuicStreamsBlockedFrame(kInvalidControlFrameId, 10, false)));
  frames.push_back(QuicFrame(QuicPaddingFrame(-1)));
  ProcessFramesPacketAtLevel(1, frames, ENCRYPTION_FORWARD_SECURE);
}

TEST_P(QuicConnectionTest,
       BundleAckWithConnectionCloseMultiplePacketNumberSpace) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  // Receives packet 1000 in initial data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  // Receives packet 2000 in application data.
  ProcessDataPacketAtLevel(2000, false, ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  const QuicErrorCode kQuicErrorCode = QUIC_INTERNAL_ERROR;
  connection_.CloseConnection(
      kQuicErrorCode, "Some random error message",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);

  EXPECT_EQ(2u, QuicConnectionPeer::GetNumEncryptionLevels(&connection_));

  TestConnectionCloseQuicErrorCode(kQuicErrorCode);
  EXPECT_EQ(1u, writer_->connection_close_frames().size());
  // Verify ack is bundled.
  EXPECT_EQ(1u, writer_->ack_frames().size());

  if (!connection_.version().CanSendCoalescedPackets()) {
    // Each connection close packet should be sent in distinct UDP packets.
    EXPECT_EQ(QuicConnectionPeer::GetNumEncryptionLevels(&connection_),
              writer_->connection_close_packets());
    EXPECT_EQ(QuicConnectionPeer::GetNumEncryptionLevels(&connection_),
              writer_->packets_write_attempts());
    return;
  }

  // A single UDP packet should be sent with multiple connection close packets
  // coalesced together.
  EXPECT_EQ(1u, writer_->packets_write_attempts());

  // Only the first packet has been processed yet.
  EXPECT_EQ(1u, writer_->connection_close_packets());

  // ProcessPacket resets the visitor and frees the coalesced packet.
  ASSERT_TRUE(writer_->coalesced_packet() != nullptr);
  auto packet = writer_->coalesced_packet()->Clone();
  writer_->framer()->ProcessPacket(*packet);
  EXPECT_EQ(1u, writer_->connection_close_packets());
  EXPECT_EQ(1u, writer_->connection_close_frames().size());
  // Verify ack is bundled.
  EXPECT_EQ(1u, writer_->ack_frames().size());
  ASSERT_TRUE(writer_->coalesced_packet() == nullptr);
}

// Regression test for b/151220135.
TEST_P(QuicConnectionTest, SendPingWhenSkipPacketNumberForPto) {
  if (!VersionSupportsMessageFrames(connection_.transport_version())) {
    return;
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kPTOS);
  connection_options.push_back(k1PTO);
  config.SetConnectionOptionsToSend(connection_options);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedMaxDatagramFrameSize(
        &config, kMaxAcceptedDatagramFrameSize);
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  connection_.OnHandshakeComplete();
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  EXPECT_EQ(MESSAGE_STATUS_SUCCESS, SendMessage("message"));
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // PTO fires, verify a PING packet gets sent because there is no data to
  // send.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, QuicPacketNumber(3), _, _));
  connection_.GetRetransmissionAlarm()->Fire();
  EXPECT_EQ(1u, connection_.GetStats().pto_count);
  EXPECT_EQ(0u, connection_.GetStats().crypto_retransmit_count);
  EXPECT_EQ(1u, writer_->ping_frames().size());
}

// Regression test for b/155757133
TEST_P(QuicConnectionTest, DonotChangeQueuedAcks) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);

  ProcessPacket(2);
  ProcessPacket(3);
  ProcessPacket(4);
  // Process a packet containing stream frame followed by ACK of packets 1.
  QuicFrames frames;
  frames.push_back(QuicFrame(QuicStreamFrame(
      QuicUtils::GetFirstBidirectionalStreamId(
          connection_.version().transport_version, Perspective::IS_CLIENT),
      false, 0u, absl::string_view())));
  QuicAckFrame ack_frame = InitAckFrame(1);
  frames.push_back(QuicFrame(&ack_frame));
  // Receiving stream frame causes something to send.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).WillOnce(Invoke([this]() {
    connection_.SendControlFrame(QuicFrame(new QuicWindowUpdateFrame(1, 0, 0)));
    // Verify now the queued ACK contains packet number 2.
    EXPECT_TRUE(QuicPacketCreatorPeer::QueuedFrames(
                    QuicConnectionPeer::GetPacketCreator(&connection_))[0]
                    .ack_frame->packets.Contains(QuicPacketNumber(2)));
  }));
  ProcessFramesPacketAtLevel(9, frames, ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(writer_->ack_frames()[0].packets.Contains(QuicPacketNumber(2)));
}

TEST_P(QuicConnectionTest, DonotExtendIdleTimeOnUndecryptablePackets) {
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);
  // Subtract a second from the idle timeout on the client side.
  QuicTime initial_deadline =
      clock_.ApproximateNow() +
      QuicTime::Delta::FromSeconds(kInitialIdleTimeoutSecs - 1);
  EXPECT_EQ(initial_deadline, connection_.GetTimeoutAlarm()->deadline());

  // Received an undecryptable packet.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
  const uint8_t tag = 0x07;
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(tag));
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  // Verify deadline does not get extended.
  EXPECT_EQ(initial_deadline, connection_.GetTimeoutAlarm()->deadline());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(1);
  QuicTime::Delta delay = initial_deadline - clock_.ApproximateNow();
  clock_.AdvanceTime(delay);
  connection_.GetTimeoutAlarm()->Fire();
  // Verify connection gets closed.
  EXPECT_FALSE(connection_.connected());
}

TEST_P(QuicConnectionTest, BundleAckWithImmediateResponse) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);

  EXPECT_CALL(visitor_, OnStreamFrame(_)).WillOnce(Invoke([this]() {
    connection_.SendControlFrame(QuicFrame(new QuicWindowUpdateFrame(1, 0, 0)));
  }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  ProcessDataPacket(1);
  // Verify ACK is bundled with WINDOW_UPDATE.
  EXPECT_FALSE(writer_->ack_frames().empty());
  EXPECT_FALSE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, AckAlarmFiresEarly) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());

  peer_framer_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                            std::make_unique<TaggingEncrypter>(0x02));
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x02));
  // Receives packet 1000 in application data.
  ProcessDataPacketAtLevel(1000, false, ENCRYPTION_ZERO_RTT);
  EXPECT_TRUE(connection_.HasPendingAcks());
  // Verify ACK deadline does not change.
  EXPECT_EQ(clock_.ApproximateNow() + kAlarmGranularity,
            connection_.GetAckAlarm()->deadline());

  // Ack alarm fires early.
  // Verify the earliest ACK is flushed.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.GetAckAlarm()->Fire();
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_EQ(clock_.ApproximateNow() + DefaultDelayedAckTime(),
            connection_.GetAckAlarm()->deadline());
}

TEST_P(QuicConnectionTest, ClientOnlyBlackholeDetectionClient) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kCBHD);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify blackhole detection is in progress.
  EXPECT_TRUE(connection_.GetBlackholeDetectorAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, ClientOnlyBlackholeDetectionServer) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  if (version().SupportsAntiAmplificationLimit()) {
    QuicConnectionPeer::SetAddressValidated(&connection_);
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(kCBHD);
  config.SetInitialReceivedConnectionOptions(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify blackhole detection is disabled.
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, 2RtoBlackholeDetection) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k2RTO);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify blackhole delay is expected.
  EXPECT_EQ(clock_.Now() +
                connection_.sent_packet_manager().GetNetworkBlackholeDelay(2),
            QuicConnectionPeer::GetBlackholeDetectionDeadline(&connection_));
}

TEST_P(QuicConnectionTest, 3RtoBlackholeDetection) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k3RTO);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify blackhole delay is expected.
  EXPECT_EQ(clock_.Now() +
                connection_.sent_packet_manager().GetNetworkBlackholeDelay(3),
            QuicConnectionPeer::GetBlackholeDetectionDeadline(&connection_));
}

TEST_P(QuicConnectionTest, 4RtoBlackholeDetection) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k4RTO);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify blackhole delay is expected.
  EXPECT_EQ(clock_.Now() +
                connection_.sent_packet_manager().GetNetworkBlackholeDelay(4),
            QuicConnectionPeer::GetBlackholeDetectionDeadline(&connection_));
}

TEST_P(QuicConnectionTest, 6RtoBlackholeDetection) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k6RTO);
  config.SetConnectionOptionsToSend(connection_options);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  EXPECT_FALSE(connection_.GetBlackholeDetectorAlarm()->IsSet());
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify blackhole delay is expected.
  EXPECT_EQ(clock_.Now() +
                connection_.sent_packet_manager().GetNetworkBlackholeDelay(6),
            QuicConnectionPeer::GetBlackholeDetectionDeadline(&connection_));
}

// Regresstion test for b/158491591.
TEST_P(QuicConnectionTest, MadeForwardProgressOnDiscardingKeys) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  use_tagging_decrypter();
  // Send handshake packet.
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  QuicConfig config;
  QuicTagVector connection_options;
  connection_options.push_back(k5RTO);
  config.SetConnectionOptionsToSend(connection_options);
  QuicConfigPeer::SetNegotiated(&config, true);
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  }
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);
  EXPECT_TRUE(connection_.BlackholeDetectionInProgress());
  // Discard handshake keys.
  connection_.OnHandshakeComplete();
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    // Verify blackhole detection stops.
    EXPECT_FALSE(connection_.BlackholeDetectionInProgress());
  } else {
    // Problematic: although there is nothing in flight, blackhole detection is
    // still in progress.
    EXPECT_TRUE(connection_.BlackholeDetectionInProgress());
  }
}

TEST_P(QuicConnectionTest, ProcessUndecryptablePacketsBasedOnEncryptionLevel) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  // SetFromConfig is always called after construction from InitializeSession.
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(AnyNumber());
  QuicConfig config;
  connection_.SetFromConfig(config);
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  connection_.RemoveDecrypter(ENCRYPTION_FORWARD_SECURE);
  use_tagging_decrypter();

  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(0x01));
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0x02));

  for (uint64_t i = 1; i <= 3; ++i) {
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_HANDSHAKE);
  }
  ProcessDataPacketAtLevel(4, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  for (uint64_t j = 5; j <= 7; ++j) {
    ProcessDataPacketAtLevel(j, !kHasStopWaiting, ENCRYPTION_HANDSHAKE);
  }
  EXPECT_EQ(7u, QuicConnectionPeer::NumUndecryptablePackets(&connection_));
  EXPECT_FALSE(connection_.GetProcessUndecryptablePacketsAlarm()->IsSet());
  SetDecrypter(ENCRYPTION_HANDSHAKE,
               std::make_unique<StrictTaggingDecrypter>(0x01));
  EXPECT_TRUE(connection_.GetProcessUndecryptablePacketsAlarm()->IsSet());
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  // Verify all ENCRYPTION_HANDSHAKE packets get processed.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(6);
  connection_.GetProcessUndecryptablePacketsAlarm()->Fire();
  EXPECT_EQ(1u, QuicConnectionPeer::NumUndecryptablePackets(&connection_));

  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  EXPECT_TRUE(connection_.GetProcessUndecryptablePacketsAlarm()->IsSet());
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x02));
  // Verify the 1-RTT packet gets processed.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  connection_.GetProcessUndecryptablePacketsAlarm()->Fire();
  EXPECT_EQ(0u, QuicConnectionPeer::NumUndecryptablePackets(&connection_));
}

TEST_P(QuicConnectionTest, ServerBundlesInitialDataWithInitialAck) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());

  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_INITIAL);
  QuicTime expected_pto_time =
      connection_.sent_packet_manager().GetRetransmissionTime();

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);
  // Verify PTO time does not change.
  EXPECT_EQ(expected_pto_time,
            connection_.sent_packet_manager().GetRetransmissionTime());

  // Receives packet 1001 in initial data.
  ProcessCryptoPacketAtLevel(1001, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());
  // Receives packet 1002 in initial data.
  ProcessCryptoPacketAtLevel(1002, ENCRYPTION_INITIAL);
  EXPECT_FALSE(writer_->ack_frames().empty());
  // Verify CRYPTO frame is bundled with INITIAL ACK.
  EXPECT_FALSE(writer_->crypto_frames().empty());
  // Verify PTO time changes.
  EXPECT_NE(expected_pto_time,
            connection_.sent_packet_manager().GetRetransmissionTime());
}

TEST_P(QuicConnectionTest, ClientBundlesHandshakeDataWithHandshakeAck) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_EQ(Perspective::IS_CLIENT, connection_.perspective());
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  SetDecrypter(ENCRYPTION_HANDSHAKE,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(0x02));
  // Receives packet 1000 in handshake data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_HANDSHAKE);
  EXPECT_TRUE(connection_.HasPendingAcks());
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);

  // Receives packet 1001 in handshake data.
  ProcessCryptoPacketAtLevel(1001, ENCRYPTION_HANDSHAKE);
  EXPECT_TRUE(connection_.HasPendingAcks());
  // Receives packet 1002 in handshake data.
  ProcessCryptoPacketAtLevel(1002, ENCRYPTION_HANDSHAKE);
  EXPECT_FALSE(writer_->ack_frames().empty());
  // Verify CRYPTO frame is bundled with HANDSHAKE ACK.
  EXPECT_FALSE(writer_->crypto_frames().empty());
}

// Regresstion test for b/156232673.
TEST_P(QuicConnectionTest, CoalescePacketOfLowerEncryptionLevel) {
  if (!connection_.version().CanSendCoalescedPackets()) {
    return;
  }
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    use_tagging_decrypter();
    connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                             std::make_unique<TaggingEncrypter>(0x01));
    connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                             std::make_unique<TaggingEncrypter>(0x02));
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    SendStreamDataToPeer(2, std::string(1286, 'a'), 0, NO_FIN, nullptr);
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
    // Try to coalesce a HANDSHAKE packet after 1-RTT packet.
    // Verify soft max packet length gets resumed and handshake packet gets
    // successfully sent.
    connection_.SendCryptoDataWithString("a", 0, ENCRYPTION_HANDSHAKE);
  }
}

// Regression test for b/160790422.
TEST_P(QuicConnectionTest, ServerRetransmitsHandshakeDataEarly) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());

  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  // Send INITIAL 1.
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_INITIAL);
  QuicTime expected_pto_time =
      connection_.sent_packet_manager().GetRetransmissionTime();

  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  // Send HANDSHAKE 2 and 3.
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);
  connection_.SendCryptoDataWithString("bar", 3, ENCRYPTION_HANDSHAKE);
  // Verify PTO time does not change.
  EXPECT_EQ(expected_pto_time,
            connection_.sent_packet_manager().GetRetransmissionTime());

  // Receives ACK for HANDSHAKE 2.
  QuicFrames frames;
  auto ack_frame = InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
  frames.push_back(QuicFrame(&ack_frame));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  ProcessFramesPacketAtLevel(30, frames, ENCRYPTION_HANDSHAKE);
  // Discard INITIAL key.
  connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
  connection_.NeuterUnencryptedPackets();
  // Receives PING from peer.
  frames.clear();
  frames.push_back(QuicFrame(QuicPingFrame()));
  frames.push_back(QuicFrame(QuicPaddingFrame(3)));
  ProcessFramesPacketAtLevel(31, frames, ENCRYPTION_HANDSHAKE);
  EXPECT_EQ(clock_.Now() + kAlarmGranularity,
            connection_.GetAckAlarm()->deadline());
  // Fire ACK alarm.
  clock_.AdvanceTime(kAlarmGranularity);
  connection_.GetAckAlarm()->Fire();
  EXPECT_FALSE(writer_->ack_frames().empty());
  // Verify handshake data gets retransmitted early.
  EXPECT_FALSE(writer_->crypto_frames().empty());
}

// Regression test for b/161228202
TEST_P(QuicConnectionTest, InflatedRttSample) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  // 30ms RTT.
  const QuicTime::Delta kTestRTT = QuicTime::Delta::FromMilliseconds(30);
  set_perspective(Perspective::IS_SERVER);
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());

  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  // Send INITIAL 1.
  std::string initial_crypto_data(512, 'a');
  connection_.SendCryptoDataWithString(initial_crypto_data, 0,
                                       ENCRYPTION_INITIAL);
  ASSERT_TRUE(connection_.sent_packet_manager()
                  .GetRetransmissionTime()
                  .IsInitialized());
  QuicTime::Delta pto_timeout =
      connection_.sent_packet_manager().GetRetransmissionTime() - clock_.Now();
  // Send Handshake 2.
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  std::string handshake_crypto_data(1024, 'a');
  connection_.SendCryptoDataWithString(handshake_crypto_data, 0,
                                       ENCRYPTION_HANDSHAKE);

  // INITIAL 1 gets lost and PTO fires.
  clock_.AdvanceTime(pto_timeout);
  connection_.GetRetransmissionAlarm()->Fire();

  clock_.AdvanceTime(kTestRTT);
  // Assume retransmitted INITIAL gets received.
  QuicFrames frames;
  QuicPacketNumber initial_retransmission =
      GetQuicReloadableFlag(quic_default_on_pto) ? QuicPacketNumber(3)
                                                 : QuicPacketNumber(4);
  auto ack_frame =
      InitAckFrame({{initial_retransmission, initial_retransmission + 1}});
  frames.push_back(QuicFrame(&ack_frame));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _))
      .Times(AnyNumber());
  ProcessFramesPacketAtLevel(1001, frames, ENCRYPTION_INITIAL);
  EXPECT_EQ(kTestRTT, rtt_stats->latest_rtt());
  // Because retransmitted INITIAL gets received so HANDSHAKE 2 gets processed.
  frames.clear();
  // HANDSHAKE 5 is also processed.
  QuicAckFrame ack_frame2 =
      InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)},
                    {initial_retransmission + 1, initial_retransmission + 2}});
  ack_frame2.ack_delay_time = QuicTime::Delta::Zero();
  frames.push_back(QuicFrame(&ack_frame2));
  ProcessFramesPacketAtLevel(1, frames, ENCRYPTION_HANDSHAKE);
  // Verify RTT inflation gets mitigated.
  EXPECT_EQ(rtt_stats->latest_rtt(), kTestRTT);
}

// Regression test for b/161228202
TEST_P(QuicConnectionTest, CoalscingPacketCausesInfiniteLoop) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  use_tagging_decrypter();
  // Receives packet 1000 in initial data.
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());

  // Set anti amplification factor to 2, such that RetransmitDataOfSpaceIfAny
  // makes no forward progress and causes infinite loop.
  SetQuicFlag(FLAGS_quic_anti_amplification_factor, 2);

  ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());

  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  // Send INITIAL 1.
  std::string initial_crypto_data(512, 'a');
  connection_.SendCryptoDataWithString(initial_crypto_data, 0,
                                       ENCRYPTION_INITIAL);
  ASSERT_TRUE(connection_.sent_packet_manager()
                  .GetRetransmissionTime()
                  .IsInitialized());
  QuicTime::Delta pto_timeout =
      connection_.sent_packet_manager().GetRetransmissionTime() - clock_.Now();
  // Send Handshake 2.
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  // Verify HANDSHAKE packet is coalesced with INITIAL retransmission.
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  std::string handshake_crypto_data(1024, 'a');
  connection_.SendCryptoDataWithString(handshake_crypto_data, 0,
                                       ENCRYPTION_HANDSHAKE);

  // INITIAL 1 gets lost and PTO fires.
  clock_.AdvanceTime(pto_timeout);
  connection_.GetRetransmissionAlarm()->Fire();
}

TEST_P(QuicConnectionTest, TestingLiveness) {
  const size_t kMinRttMs = 40;
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(kMinRttMs),
                       QuicTime::Delta::Zero(), QuicTime::Zero());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;

  CryptoHandshakeMessage msg;
  std::string error_details;
  QuicConfig client_config;
  client_config.SetInitialStreamFlowControlWindowToSend(
      kInitialStreamFlowControlWindowForTest);
  client_config.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest);
  client_config.SetIdleNetworkTimeout(QuicTime::Delta::FromSeconds(30));
  client_config.ToHandshakeMessage(&msg, connection_.transport_version());
  const QuicErrorCode error =
      config.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());

  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }

  connection_.SetFromConfig(config);
  connection_.OnHandshakeComplete();
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  ASSERT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_FALSE(connection_.MaybeTestLiveness());

  QuicTime deadline = connection_.GetTimeoutAlarm()->deadline();
  QuicTime::Delta timeout = deadline - clock_.ApproximateNow();
  // Advance time to near the idle timeout.
  clock_.AdvanceTime(timeout - QuicTime::Delta::FromMilliseconds(1));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  EXPECT_TRUE(connection_.MaybeTestLiveness());
  // Verify idle deadline does not change.
  EXPECT_EQ(deadline, connection_.GetTimeoutAlarm()->deadline());
}

TEST_P(QuicConnectionTest, SilentIdleTimeout) {
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  if (version().SupportsAntiAmplificationLimit()) {
    QuicConnectionPeer::SetAddressValidated(&connection_);
  }

  QuicConfig config;
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(&config,
                                                         QuicConnectionId());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  EXPECT_TRUE(connection_.connected());
  EXPECT_TRUE(connection_.GetTimeoutAlarm()->IsSet());

  if (version().handshake_protocol == PROTOCOL_TLS1_3) {
    EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  }
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.GetTimeoutAlarm()->Fire();
  // Verify the connection close packets get serialized and added to
  // termination packets list.
  EXPECT_NE(nullptr,
            QuicConnectionPeer::GetConnectionClosePacket(&connection_));
}

TEST_P(QuicConnectionTest, DonotSendPing) {
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.OnHandshakeComplete();
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(connection_.GetPingAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(0, connection_.transport_version()),
      "GET /", 0, FIN, nullptr);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(15),
            connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  // Now recevie an ACK and response of the previous packet, which will move the
  // ping alarm forward.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  QuicFrames frames;
  QuicAckFrame ack_frame = InitAckFrame(1);
  frames.push_back(QuicFrame(&ack_frame));
  frames.push_back(QuicFrame(QuicStreamFrame(
      GetNthClientInitiatedStreamId(0, connection_.transport_version()), true,
      0u, absl::string_view())));
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessFramesPacketAtLevel(1, frames, ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(connection_.GetPingAlarm()->IsSet());
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());
  // The ping timer is set slightly less than 15 seconds in the future, because
  // of the 1s ping timer alarm granularity.
  EXPECT_EQ(
      QuicTime::Delta::FromSeconds(15) - QuicTime::Delta::FromMilliseconds(5),
      connection_.GetPingAlarm()->deadline() - clock_.ApproximateNow());

  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(15));
  // Suppose now ShouldKeepConnectionAlive returns false.
  EXPECT_CALL(visitor_, ShouldKeepConnectionAlive())
      .WillRepeatedly(Return(false));
  // Verify PING does not get sent.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0);
  connection_.GetPingAlarm()->Fire();
}

// Regression test for b/159698337
TEST_P(QuicConnectionTest, DuplicateAckCausesLostPackets) {
  if (!GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    return;
  }
  // Finish handshake.
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  notifier_.NeuterUnencryptedData();
  connection_.NeuterUnencryptedPackets();
  connection_.OnHandshakeComplete();
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));

  std::string data(1200, 'a');
  // Send data packets 1 - 5.
  for (size_t i = 0; i < 5; ++i) {
    SendStreamDataToPeer(
        GetNthClientInitiatedStreamId(1, connection_.transport_version()), data,
        i * 1200, i == 4 ? FIN : NO_FIN, nullptr);
  }
  ASSERT_TRUE(connection_.BlackholeDetectionInProgress());

  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _)).Times(3);

  // ACK packet 5 and 1 and 2 are detected lost.
  QuicAckFrame frame =
      InitAckFrame({{QuicPacketNumber(5), QuicPacketNumber(6)}});
  LostPacketVector lost_packets;
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(1), kMaxOutgoingPacketSize));
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(2), kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .Times(AnyNumber())
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  ProcessAckPacket(1, &frame);
  EXPECT_TRUE(connection_.BlackholeDetectionInProgress());
  QuicAlarm* retransmission_alarm = connection_.GetRetransmissionAlarm();
  EXPECT_TRUE(retransmission_alarm->IsSet());

  // ACK packet 1 - 5 and 7.
  QuicAckFrame frame2 =
      InitAckFrame({{QuicPacketNumber(1), QuicPacketNumber(6)},
                    {QuicPacketNumber(7), QuicPacketNumber(8)}});
  ProcessAckPacket(2, &frame2);
  EXPECT_TRUE(connection_.BlackholeDetectionInProgress());

  // ACK packet 7 again and assume packet 6 is detected lost.
  QuicAckFrame frame3 =
      InitAckFrame({{QuicPacketNumber(7), QuicPacketNumber(8)}});
  lost_packets.clear();
  lost_packets.push_back(
      LostPacket(QuicPacketNumber(6), kMaxOutgoingPacketSize));
  EXPECT_CALL(*loss_algorithm_, DetectLosses(_, _, _, _, _, _))
      .Times(AnyNumber())
      .WillOnce(DoAll(SetArgPointee<5>(lost_packets),
                      Return(LossDetectionInterface::DetectionStats())));
  ProcessAckPacket(3, &frame3);
  // Make sure loss detection is cancelled even there is no new acked packets.
  EXPECT_FALSE(connection_.BlackholeDetectionInProgress());
}

TEST_P(QuicConnectionTest, ShorterIdleTimeoutOnSentPackets) {
  EXPECT_TRUE(connection_.connected());
  RttStats* rtt_stats = const_cast<RttStats*>(manager_->GetRttStats());
  rtt_stats->UpdateRtt(QuicTime::Delta::FromMilliseconds(100),
                       QuicTime::Delta::Zero(), QuicTime::Zero());

  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  config.SetClientConnectionOptions(QuicTagVector{kFIDT});
  QuicConfigPeer::SetNegotiated(&config, true);
  if (GetQuicReloadableFlag(quic_default_enable_5rto_blackhole_detection2)) {
    EXPECT_CALL(visitor_, GetHandshakeState())
        .WillRepeatedly(Return(HANDSHAKE_COMPLETE));
  }
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  connection_.SetFromConfig(config);

  ASSERT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  // Send a packet close to timeout.
  QuicTime::Delta timeout =
      connection_.GetTimeoutAlarm()->deadline() - clock_.Now();
  clock_.AdvanceTime(timeout - QuicTime::Delta::FromSeconds(1));
  // Send stream data.
  SendStreamDataToPeer(
      GetNthClientInitiatedStreamId(1, connection_.transport_version()), "foo",
      0, FIN, nullptr);
  // Verify this sent packet does not extend idle timeout since 1s is > PTO
  // delay.
  ASSERT_TRUE(connection_.GetTimeoutAlarm()->IsSet());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(1),
            connection_.GetTimeoutAlarm()->deadline() - clock_.Now());

  // Received an ACK 100ms later.
  clock_.AdvanceTime(timeout - QuicTime::Delta::FromMilliseconds(100));
  QuicAckFrame ack = InitAckFrame(1);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  ProcessAckPacket(1, &ack);
  // Verify idle timeout gets extended.
  EXPECT_EQ(clock_.Now() + timeout, connection_.GetTimeoutAlarm()->deadline());
}

// Regression test for b/166255274
TEST_P(QuicConnectionTest,
       ReserializeInitialPacketInCoalescerAfterDiscardingInitialKey) {
  if (!connection_.version().CanSendCoalescedPackets()) {
    return;
  }
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).WillOnce(Invoke([this]() {
    connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
    connection_.NeuterUnencryptedPackets();
  }));
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);
    // Verify the packet is on hold.
    EXPECT_EQ(0u, writer_->packets_write_attempts());
    // Flush pending ACKs.
    connection_.GetAckAlarm()->Fire();
  }
  EXPECT_FALSE(connection_.packet_creator().HasPendingFrames());
  // The ACK frame is deleted along with initial_packet_ in coalescer. Sending
  // connection close would cause this (released) ACK frame be serialized (and
  // crashes).
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(1000, false, ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(connection_.connected());
}

TEST_P(QuicConnectionTest, PathValidationOnNewSocketSuccess) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(1u, new_writer.packets_write_attempts());
        EXPECT_EQ(1u, new_writer.path_challenge_frames().size());
        EXPECT_EQ(1u, new_writer.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress.host(),
                  new_writer.last_write_source_address());
      }));
  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));
  EXPECT_EQ(0u, writer_->packets_write_attempts());

  QuicFrames frames;
  frames.push_back(QuicFrame(new QuicPathResponseFrame(
      99, new_writer.path_challenge_frames().front().data_buffer)));
  ProcessFramesPacketWithAddresses(frames, kNewSelfAddress, kPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(success);
}

TEST_P(QuicConnectionTest, NewPathValidationCancelsPreviousOne) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(1u, new_writer.packets_write_attempts());
        EXPECT_EQ(1u, new_writer.path_challenge_frames().size());
        EXPECT_EQ(1u, new_writer.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress.host(),
                  new_writer.last_write_source_address());
      }));
  bool success = true;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));
  EXPECT_EQ(0u, writer_->packets_write_attempts());

  // Start another path validation request.
  const QuicSocketAddress kNewSelfAddress2(QuicIpAddress::Any4(), 12346);
  EXPECT_NE(kNewSelfAddress2, connection_.self_address());
  TestPacketWriter new_writer2(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(1u, new_writer2.packets_write_attempts());
        EXPECT_EQ(1u, new_writer2.path_challenge_frames().size());
        EXPECT_EQ(1u, new_writer2.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress2.host(),
                  new_writer2.last_write_source_address());
      }));
  bool success2 = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress2, connection_.peer_address(), &new_writer2),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress2, connection_.peer_address(), &success2));
  EXPECT_FALSE(success);
  EXPECT_TRUE(connection_.HasPendingPathValidation());
}

TEST_P(QuicConnectionTest, PathValidationReceivesStatelessReset) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  const QuicUint128 kTestStatelessResetToken = 1010101;
  QuicConfig config;
  QuicConfigPeer::SetReceivedStatelessResetToken(&config,
                                                 kTestStatelessResetToken);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(1u, new_writer.packets_write_attempts());
        EXPECT_EQ(1u, new_writer.path_challenge_frames().size());
        EXPECT_EQ(1u, new_writer.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress.host(),
                  new_writer.last_write_source_address());
      }));
  bool success = true;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));
  EXPECT_EQ(0u, writer_->packets_write_attempts());
  EXPECT_TRUE(connection_.HasPendingPathValidation());

  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildIetfStatelessResetPacket(connection_id_,
                                                kTestStatelessResetToken));
  std::unique_ptr<QuicReceivedPacket> received(
      ConstructReceivedPacket(*packet, QuicTime::Zero()));
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _)).Times(0);
  connection_.ProcessUdpPacket(kNewSelfAddress, kPeerAddress, *received);
  EXPECT_FALSE(connection_.HasPendingPathValidation());
  EXPECT_FALSE(success);
}

// Tests that PATH_CHALLENGE is dropped if it is sent via a blocked alternative
// writer.
TEST_P(QuicConnectionTest, SendPathChallengeUsingBlockedNewSocket) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  new_writer.BlockOnNextWrite();
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(0);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(Invoke([&]() {
        // Even though the socket is blocked, the PATH_CHALLENGE should still be
        // treated as sent.
        EXPECT_EQ(1u, new_writer.packets_write_attempts());
        EXPECT_EQ(1u, new_writer.path_challenge_frames().size());
        EXPECT_EQ(1u, new_writer.padding_frames().size());
        EXPECT_EQ(kNewSelfAddress.host(),
                  new_writer.last_write_source_address());
      }));
  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));
  EXPECT_EQ(0u, writer_->packets_write_attempts());

  new_writer.SetWritable();
  // Write event on the default socket shouldn't make any difference.
  connection_.OnCanWrite();
  EXPECT_EQ(0u, writer_->packets_write_attempts());
  EXPECT_EQ(1u, new_writer.packets_write_attempts());
}

//  Tests that PATH_CHALLENGE is dropped if it is sent via the default writer
//  and the writer is blocked.
TEST_P(QuicConnectionTest, SendPathChallengeUsingBlockedDefaultSocket) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Any4(), 12345);
  writer_->BlockOnNextWrite();
  // 1st time is after writer returns WRITE_STATUS_BLOCKED. 2nd time is in
  // ShouldGeneratePacket().
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AtLeast(2));
  QuicPathFrameBuffer path_challenge_payload{0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([&]() {
        // This packet isn't sent actually, instead it is buffered in the
        // connection.
        EXPECT_EQ(1u, writer_->packets_write_attempts());
        if (connection_.validate_client_address()) {
          EXPECT_EQ(1u, writer_->path_response_frames().size());
          EXPECT_EQ(0,
                    memcmp(&path_challenge_payload,
                           &writer_->path_response_frames().front().data_buffer,
                           sizeof(path_challenge_payload)));
        }
        EXPECT_EQ(1u, writer_->path_challenge_frames().size());
        EXPECT_EQ(1u, writer_->padding_frames().size());
        EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
      }))
      .WillRepeatedly(Invoke([&]() {
        // Only one PATH_CHALLENGE should be sent out.
        EXPECT_EQ(0u, writer_->path_challenge_frames().size());
      }));
  bool success = false;
  if (connection_.validate_client_address()) {
    // Receiving a PATH_CHALLENGE from the new peer address should trigger
    // address validation.
    QuicFrames frames;
    frames.push_back(
        QuicFrame(new QuicPathChallengeFrame(0, path_challenge_payload)));
    ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                     ENCRYPTION_FORWARD_SECURE);
  } else {
    // Manually start to validate the new peer address.
    connection_.ValidatePath(
        std::make_unique<TestQuicPathValidationContext>(
            connection_.self_address(), kNewPeerAddress, writer_.get()),
        std::make_unique<TestValidationResultDelegate>(
            connection_.self_address(), kNewPeerAddress, &success));
  }
  EXPECT_EQ(1u, writer_->packets_write_attempts());

  // Try again with the new socket blocked from the beginning. The 2nd
  // PATH_CHALLENGE shouldn't be serialized, but be dropped.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(3 * kInitialRttMs));
  static_cast<test::MockRandom*>(helper_->GetRandomGenerator())->ChangeValue();
  static_cast<TestAlarmFactory::TestAlarm*>(
      QuicPathValidatorPeer::retry_timer(
          QuicConnectionPeer::path_validator(&connection_)))
      ->Fire();

  // No more write attempt should be made.
  EXPECT_EQ(1u, writer_->packets_write_attempts());

  writer_->SetWritable();
  // OnCanWrite() should actually write out the 1st PATH_CHALLENGE packet
  // buffered earlier, thus incrementing the write counter. It may also send
  // ACKs to previously received packets.
  connection_.OnCanWrite();
  EXPECT_LE(2u, writer_->packets_write_attempts());
}

// Tests that write error on the alternate socket should be ignored.
TEST_P(QuicConnectionTest, SendPathChallengeFailOnNewSocket) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  new_writer.SetShouldWriteFail();
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(0);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0u);

  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));
  EXPECT_EQ(1u, new_writer.packets_write_attempts());
  EXPECT_EQ(1u, new_writer.path_challenge_frames().size());
  EXPECT_EQ(1u, new_writer.padding_frames().size());
  EXPECT_EQ(kNewSelfAddress.host(), new_writer.last_write_source_address());

  EXPECT_EQ(0u, writer_->packets_write_attempts());
  //  Regardless of the write error, the connection should still be connected.
  EXPECT_TRUE(connection_.connected());
}

// Tests that write error while sending PATH_CHALLANGE from the default socket
// should close the connection.
TEST_P(QuicConnectionTest, SendPathChallengeFailOnDefaultPath) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);

  writer_->SetShouldWriteFail();
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(
          Invoke([](QuicConnectionCloseFrame frame, ConnectionCloseSource) {
            EXPECT_EQ(QUIC_PACKET_WRITE_ERROR, frame.quic_error_code);
          }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0u);
  {
    // Add a flusher to force flush, otherwise the frames will remain in the
    // packet creator.
    bool success = false;
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    connection_.ValidatePath(
        std::make_unique<TestQuicPathValidationContext>(
            connection_.self_address(), connection_.peer_address(),
            writer_.get()),
        std::make_unique<TestValidationResultDelegate>(
            connection_.self_address(), connection_.peer_address(), &success));
  }
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_EQ(1u, writer_->path_challenge_frames().size());
  EXPECT_EQ(1u, writer_->padding_frames().size());
  EXPECT_EQ(connection_.peer_address(), writer_->last_write_peer_address());
  EXPECT_FALSE(connection_.connected());
  // Closing connection should abandon ongoing path validation.
  EXPECT_FALSE(connection_.HasPendingPathValidation());
}

TEST_P(QuicConnectionTest, SendPathChallengeFailOnAlternativePeerAddress) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);

  writer_->SetShouldWriteFail();
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .WillOnce(
          Invoke([](QuicConnectionCloseFrame frame, ConnectionCloseSource) {
            EXPECT_EQ(QUIC_PACKET_WRITE_ERROR, frame.quic_error_code);
          }));
  // Sending PATH_CHALLENGE to trigger a flush write which will fail and close
  // the connection.
  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          connection_.self_address(), kNewPeerAddress, writer_.get()),
      std::make_unique<TestValidationResultDelegate>(
          connection_.self_address(), kNewPeerAddress, &success));

  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_FALSE(connection_.HasPendingPathValidation());
  EXPECT_EQ(1u, writer_->path_challenge_frames().size());
  EXPECT_EQ(1u, writer_->padding_frames().size());
  EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
  EXPECT_FALSE(connection_.connected());
}

TEST_P(QuicConnectionTest,
       SendPathChallengeFailPacketTooBigOnAlternativePeerAddress) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);

  writer_->SetShouldWriteFail();
  writer_->SetWriteError(EMSGSIZE);
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_CALL(visitor_, OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF))
      .Times(0u);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(0u);
  // Sending PATH_CHALLENGE to trigger a flush write which will fail with
  // MSG_TOO_BIG.
  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          connection_.self_address(), kNewPeerAddress, writer_.get()),
      std::make_unique<TestValidationResultDelegate>(
          connection_.self_address(), kNewPeerAddress, &success));
  EXPECT_TRUE(connection_.HasPendingPathValidation());
  // Connection shouldn't be closed.
  EXPECT_TRUE(connection_.connected());
  EXPECT_EQ(1u, writer_->packets_write_attempts());
  EXPECT_EQ(1u, writer_->path_challenge_frames().size());
  EXPECT_EQ(1u, writer_->padding_frames().size());
  EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
}

// Check that if there are two PATH_CHALLENGE frames in the packet, the latter
// one is ignored.
TEST_P(QuicConnectionTest, ReceiveMultiplePathChallenge) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version)) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);

  QuicPathFrameBuffer path_frame_buffer1{0, 1, 2, 3, 4, 5, 6, 7};
  QuicPathFrameBuffer path_frame_buffer2{8, 9, 10, 11, 12, 13, 14, 15};
  QuicFrames frames;
  frames.push_back(
      QuicFrame(new QuicPathChallengeFrame(0, path_frame_buffer1)));
  frames.push_back(
      QuicFrame(new QuicPathChallengeFrame(0, path_frame_buffer2)));
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback6(),
                                          /*port=*/23456);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0);

  // Expect 2 packets to be sent: the first are padded PATH_RESPONSE(s) to the
  // alternative peer address. The 2nd is a ACK-only packet to the original
  // peer address.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(2)
      .WillOnce(Invoke([=]() {
        EXPECT_EQ((connection_.send_path_response() ? 1u : 2u),
                  writer_->path_response_frames().size());
        // The final check is to ensure that the random data in the response
        // matches the random data from the challenge.
        EXPECT_EQ(0,
                  memcmp(path_frame_buffer1.data(),
                         &(writer_->path_response_frames().front().data_buffer),
                         sizeof(path_frame_buffer1)));
        if (!connection_.send_path_response()) {
          EXPECT_EQ(
              0, memcmp(path_frame_buffer2.data(),
                        &(writer_->path_response_frames().back().data_buffer),
                        sizeof(path_frame_buffer2)));
        } else {
          EXPECT_EQ(1u, writer_->padding_frames().size());
        }
        EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
      }))
      .WillOnce(Invoke([=]() {
        // The last write of ACK-only packet should still use the old peer
        // address.
        EXPECT_EQ(kPeerAddress, writer_->last_write_peer_address());
      }));
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
}

TEST_P(QuicConnectionTest, ReceiveStreamFrameBeforePathChallenge) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.send_path_response()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);

  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  QuicPathFrameBuffer path_frame_buffer{0, 1, 2, 3, 4, 5, 6, 7};
  frames.push_back(QuicFrame(new QuicPathChallengeFrame(0, path_frame_buffer)));
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback4(),
                                          /*port=*/23456);

  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE));
  EXPECT_CALL(*send_algorithm_, OnConnectionMigration())
      .Times(connection_.validate_client_address() ? 0u : 1u);
  EXPECT_CALL(visitor_, OnStreamFrame(_))
      .WillOnce(Invoke([=](const QuicStreamFrame& frame) {
        // Send some data on the stream. The STREAM_FRAME should be built into
        // one packet together with the latter PATH_RESPONSE and PATH_CHALLENGE.
        std::string data{"response body"};
        struct iovec iov;
        MakeIOVector(data, &iov);
        connection_.producer()->SaveStreamData(frame.stream_id, &iov, 1, 0u,
                                               data.length());
        return notifier_.WriteOrBufferData(frame.stream_id, data.length(),
                                           NO_FIN);
      }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(connection_.validate_client_address() ? 0u : 1u);
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);

  // Verify that this packet contains a STREAM_FRAME and a
  // PATH_RESPONSE_FRAME.
  EXPECT_EQ(1u, writer_->stream_frames().size());
  EXPECT_EQ(1u, writer_->path_response_frames().size());
  EXPECT_EQ(connection_.validate_client_address() ? 1u : 0u,
            writer_->path_challenge_frames().size());
  // The final check is to ensure that the random data in the response
  // matches the random data from the challenge.
  EXPECT_EQ(0, memcmp(path_frame_buffer.data(),
                      &(writer_->path_response_frames().front().data_buffer),
                      sizeof(path_frame_buffer)));
  EXPECT_EQ(connection_.validate_client_address() ? 1u : 0u,
            writer_->path_challenge_frames().size());
  EXPECT_EQ(1u, writer_->padding_frames().size());
  EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
  if (connection_.validate_client_address()) {
    EXPECT_TRUE(connection_.HasPendingPathValidation());
  }
}

TEST_P(QuicConnectionTest, ReceiveStreamFrameFollowingPathChallenge) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.send_path_response()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);

  QuicFrames frames;
  QuicPathFrameBuffer path_frame_buffer{0, 1, 2, 3, 4, 5, 6, 7};
  frames.push_back(QuicFrame(new QuicPathChallengeFrame(0, path_frame_buffer)));
  // PATH_RESPONSE should be flushed out before the rest packet is parsed.
  frames.push_back(QuicFrame(frame1_));
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback4(),
                                          /*port=*/23456);
  QuicByteCount received_packet_size;
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(AtLeast(1u))
      .WillOnce(Invoke([=, &received_packet_size]() {
        // Verify that this packet contains a PATH_RESPONSE_FRAME.
        EXPECT_EQ(0u, writer_->stream_frames().size());
        EXPECT_EQ(1u, writer_->path_response_frames().size());
        // The final check is to ensure that the random data in the response
        // matches the random data from the challenge.
        EXPECT_EQ(0,
                  memcmp(path_frame_buffer.data(),
                         &(writer_->path_response_frames().front().data_buffer),
                         sizeof(path_frame_buffer)));
        EXPECT_EQ(connection_.validate_client_address() ? 1u : 0u,
                  writer_->path_challenge_frames().size());
        EXPECT_EQ(1u, writer_->padding_frames().size());
        EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
        received_packet_size =
            QuicConnectionPeer::BytesReceivedOnAlternativePath(&connection_);
      }));
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE));
  EXPECT_CALL(*send_algorithm_, OnConnectionMigration())
      .Times(connection_.validate_client_address() ? 0u : 1u);
  EXPECT_CALL(visitor_, OnStreamFrame(_))
      .WillOnce(Invoke([=](const QuicStreamFrame& frame) {
        // Send some data on the stream. The STREAM_FRAME should be built into a
        // new packet but throttled by anti-amplifciation limit.
        std::string data{"response body"};
        struct iovec iov;
        MakeIOVector(data, &iov);
        connection_.producer()->SaveStreamData(frame.stream_id, &iov, 1, 0u,
                                               data.length());
        return notifier_.WriteOrBufferData(frame.stream_id, data.length(),
                                           NO_FIN);
      }));

  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  if (!connection_.validate_client_address()) {
    return;
  }
  EXPECT_TRUE(connection_.HasPendingPathValidation());
  EXPECT_EQ(0u,
            QuicConnectionPeer::BytesReceivedOnAlternativePath(&connection_));
  EXPECT_EQ(
      received_packet_size,
      QuicConnectionPeer::BytesReceivedBeforeAddressValidation(&connection_));
}

// Tests that a PATH_CHALLENGE is received in between other frames in an out of
// order packet.
TEST_P(QuicConnectionTest, PathChallengeWithDataInOutOfOrderPacket) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.send_path_response()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);

  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  QuicPathFrameBuffer path_frame_buffer{0, 1, 2, 3, 4, 5, 6, 7};
  frames.push_back(QuicFrame(new QuicPathChallengeFrame(0, path_frame_buffer)));
  frames.push_back(QuicFrame(frame2_));
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback6(),
                                          /*port=*/23456);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0u);
  EXPECT_CALL(visitor_, OnStreamFrame(_))
      .Times(2)
      .WillRepeatedly(Invoke([=](const QuicStreamFrame& frame) {
        // Send some data on the stream. The STREAM_FRAME should be built into
        // one packet together with the latter PATH_RESPONSE.
        std::string data{"response body"};
        struct iovec iov;
        MakeIOVector(data, &iov);
        connection_.producer()->SaveStreamData(frame.stream_id, &iov, 1, 0u,
                                               data.length());
        return notifier_.WriteOrBufferData(frame.stream_id, data.length(),
                                           NO_FIN);
      }));
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(Invoke([=]() {
        // Verify that this packet contains a STREAM_FRAME and is sent to the
        // original peer address.
        EXPECT_EQ(1u, writer_->stream_frames().size());
        // No connection migration should happen because the packet is received
        // out of order.
        EXPECT_EQ(kPeerAddress, writer_->last_write_peer_address());
      }))
      .WillOnce(Invoke([=]() {
        EXPECT_EQ(1u, writer_->path_response_frames().size());
        // The final check is to ensure that the random data in the response
        // matches the random data from the challenge.
        EXPECT_EQ(0,
                  memcmp(path_frame_buffer.data(),
                         &(writer_->path_response_frames().front().data_buffer),
                         sizeof(path_frame_buffer)));
        EXPECT_EQ(1u, writer_->padding_frames().size());
        // PATH_RESPONSE should be sent in another packet to a different peer
        // address.
        EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
      }))
      .WillOnce(Invoke([=]() {
        // Verify that this packet contains a STREAM_FRAME and is sent to the
        // original peer address.
        EXPECT_EQ(1u, writer_->stream_frames().size());
        // No connection migration should happen because the packet is received
        // out of order.
        EXPECT_EQ(kPeerAddress, writer_->last_write_peer_address());
      }));
  // Lower the packet number so that receiving this packet shouldn't trigger
  // peer migration.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 1);
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
}

// Tests that a PATH_CHALLENGE is cached if its PATH_RESPONSE can't be sent.
TEST_P(QuicConnectionTest, FailToWritePathResponse) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.send_path_response()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);

  QuicFrames frames;
  QuicPathFrameBuffer path_frame_buffer{0, 1, 2, 3, 4, 5, 6, 7};
  frames.push_back(QuicFrame(new QuicPathChallengeFrame(0, path_frame_buffer)));
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback6(),
                                          /*port=*/23456);

  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0u);
  // Lower the packet number so that receiving this packet shouldn't trigger
  // peer migration.
  QuicPacketCreatorPeer::SetPacketNumber(&peer_creator_, 1);
  EXPECT_CALL(visitor_, OnWriteBlocked()).Times(AtLeast(1));
  writer_->SetWriteBlocked();
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);

  EXPECT_EQ(
      1u, QuicConnectionPeer::NumPendingPathChallengesToResponse(&connection_));

  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AtLeast(1));
  writer_->SetWritable();
  connection_.OnCanWrite();
  EXPECT_EQ(1u, writer_->path_response_frames().size());
  // The final check is to ensure that the random data in the response
  // matches the random data from the challenge.
  EXPECT_EQ(0, memcmp(path_frame_buffer.data(),
                      &(writer_->path_response_frames().front().data_buffer),
                      sizeof(path_frame_buffer)));
  EXPECT_EQ(1u, writer_->padding_frames().size());
  // PATH_RESPONSE should be sent in another packet to a different peer
  // address.
  EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
  EXPECT_EQ(
      0u, QuicConnectionPeer::NumPendingPathChallengesToResponse(&connection_));
}

// Regression test for b/168101557.
TEST_P(QuicConnectionTest, HandshakeDataDoesNotGetPtoed) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  if (QuicVersionUsesCryptoFrames(connection_.transport_version())) {
    EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  }
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(AnyNumber());
  use_tagging_decrypter();
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.HasPendingAcks());

  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  // Send INITIAL 1.
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_INITIAL);

  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  SetDecrypter(ENCRYPTION_HANDSHAKE,
               std::make_unique<StrictTaggingDecrypter>(0x02));
  // Send HANDSHAKE packets.
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(1);
  connection_.SendCryptoDataWithString("foo", 0, ENCRYPTION_HANDSHAKE);

  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x03));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  // Send half RTT packet.
  connection_.SendStreamDataWithString(2, "foo", 0, NO_FIN);

  // Receives HANDSHAKE 1.
  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(0x02));
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_HANDSHAKE);
  // Discard INITIAL key.
  connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
  connection_.NeuterUnencryptedPackets();
  // Verify there is pending ACK.
  ASSERT_TRUE(connection_.HasPendingAcks());
  // Set the send alarm.
  connection_.GetSendAlarm()->Set(clock_.ApproximateNow());

  // Fire ACK alarm.
  connection_.GetAckAlarm()->Fire();
  // Verify 1-RTT packet is coalesced with handshake packet.
  EXPECT_EQ(0x03030303u, writer_->final_bytes_of_last_packet());
  connection_.GetSendAlarm()->Fire();

  ASSERT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  connection_.GetRetransmissionAlarm()->Fire();
  // Verify a handshake packet gets PTOed and 1-RTT packet gets coalesced.
  EXPECT_EQ(0x03030303u, writer_->final_bytes_of_last_packet());
}

// Regression test for b/168294218.
TEST_P(QuicConnectionTest, CoalescerHandlesInitialKeyDiscard) {
  if (!connection_.version().CanSendCoalescedPackets()) {
    return;
  }
  SetQuicReloadableFlag(quic_discard_initial_packet_with_key_dropped, true);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(2);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).WillOnce(Invoke([this]() {
    connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
    connection_.NeuterUnencryptedPackets();
  }));
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());

  EXPECT_EQ(0u, connection_.GetStats().packets_discarded);
  {
    QuicConnection::ScopedPacketFlusher flusher(&connection_);
    use_tagging_decrypter();
    ProcessCryptoPacketAtLevel(1000, ENCRYPTION_INITIAL);
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
    connection_.SetEncrypter(ENCRYPTION_INITIAL,
                             std::make_unique<TaggingEncrypter>(0x01));
    connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                             std::make_unique<TaggingEncrypter>(0x02));
    connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
    connection_.SendCryptoDataWithString(std::string(1300, 'a'), 0);
    // Verify this packet is on hold.
    EXPECT_EQ(0u, writer_->packets_write_attempts());
  }
  EXPECT_TRUE(connection_.connected());
}

// Regresstion test for b/168294218
TEST_P(QuicConnectionTest, ZeroRttRejectionAndMissingInitialKeys) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  // Not defer send in response to packet.
  connection_.set_defer_send_in_response_to_packets(false);
  EXPECT_CALL(visitor_, OnHandshakePacketSent()).WillOnce(Invoke([this]() {
    connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
    connection_.NeuterUnencryptedPackets();
  }));
  EXPECT_CALL(visitor_, OnCryptoFrame(_))
      .WillRepeatedly(Invoke([=](const QuicCryptoFrame& frame) {
        if (frame.level == ENCRYPTION_HANDSHAKE) {
          // 0-RTT gets rejected.
          connection_.MarkZeroRttPacketsForRetransmission(0);
          // Send Crypto data.
          connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                                   std::make_unique<TaggingEncrypter>(0x03));
          connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
          connection_.SendCryptoStreamData();
          connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                                   std::make_unique<TaggingEncrypter>(0x04));
          connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
          // Retransmit rejected 0-RTT packets.
          connection_.OnCanWrite();
          // Advance INITIAL ack delay to trigger initial ACK to be sent AFTER
          // the retransmission of rejected 0-RTT packets while the HANDSHAKE
          // packet is still in the coalescer, such that the INITIAL key gets
          // dropped between SendAllPendingAcks and actually send the ack frame,
          // bummer.
          clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1));
        }
      }));
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SendCryptoStreamData();
  // Send 0-RTT packet.
  connection_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  connection_.SendStreamDataWithString(2, "foo", 0, NO_FIN);

  QuicAckFrame frame1 = InitAckFrame(1);
  // Received ACK for packet 1.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
  ProcessFramePacketAtLevel(1, QuicFrame(&frame1), ENCRYPTION_INITIAL);
  EXPECT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());

  // Fire retransmission alarm.
  connection_.GetRetransmissionAlarm()->Fire();

  QuicFrames frames1;
  frames1.push_back(QuicFrame(&crypto_frame_));
  QuicFrames frames2;
  QuicCryptoFrame crypto_frame(ENCRYPTION_HANDSHAKE, 0,
                               absl::string_view(data1));
  frames2.push_back(QuicFrame(&crypto_frame));
  ProcessCoalescedPacket(
      {{2, frames1, ENCRYPTION_INITIAL}, {3, frames2, ENCRYPTION_HANDSHAKE}});
}

TEST_P(QuicConnectionTest, OnZeroRttPacketAcked) {
  if (!connection_.version().UsesTls()) {
    return;
  }
  MockQuicConnectionDebugVisitor debug_visitor;
  connection_.set_debug_visitor(&debug_visitor);
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SendCryptoStreamData();
  // Send 0-RTT packet.
  connection_.SetEncrypter(ENCRYPTION_ZERO_RTT,
                           std::make_unique<TaggingEncrypter>(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
  connection_.SendStreamDataWithString(2, "foo", 0, NO_FIN);
  connection_.SendStreamDataWithString(4, "bar", 0, NO_FIN);
  // Received ACK for packet 1, HANDSHAKE packet and 1-RTT ACK.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _))
      .Times(AnyNumber());
  QuicFrames frames1;
  QuicAckFrame ack_frame1 = InitAckFrame(1);
  frames1.push_back(QuicFrame(&ack_frame1));

  QuicFrames frames2;
  QuicCryptoFrame crypto_frame(ENCRYPTION_HANDSHAKE, 0,
                               absl::string_view(data1));
  frames2.push_back(QuicFrame(&crypto_frame));
  EXPECT_CALL(debug_visitor, OnZeroRttPacketAcked()).Times(0);
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(1);
  ProcessCoalescedPacket(
      {{1, frames1, ENCRYPTION_INITIAL}, {2, frames2, ENCRYPTION_HANDSHAKE}});

  QuicFrames frames3;
  QuicAckFrame ack_frame2 =
      InitAckFrame({{QuicPacketNumber(2), QuicPacketNumber(3)}});
  frames3.push_back(QuicFrame(&ack_frame2));
  EXPECT_CALL(debug_visitor, OnZeroRttPacketAcked()).Times(1);
  ProcessCoalescedPacket({{3, frames3, ENCRYPTION_FORWARD_SECURE}});

  QuicFrames frames4;
  QuicAckFrame ack_frame3 =
      InitAckFrame({{QuicPacketNumber(3), QuicPacketNumber(4)}});
  frames4.push_back(QuicFrame(&ack_frame3));
  EXPECT_CALL(debug_visitor, OnZeroRttPacketAcked()).Times(0);
  ProcessCoalescedPacket({{4, frames4, ENCRYPTION_FORWARD_SECURE}});
}

TEST_P(QuicConnectionTest, InitiateKeyUpdate) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  TransportParameters params;
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  std::string error_details;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  EXPECT_FALSE(connection_.IsKeyUpdateAllowed());

  MockFramerVisitor peer_framer_visitor_;
  peer_framer_.set_visitor(&peer_framer_visitor_);

  use_tagging_decrypter();

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x01));
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypter>(0x01));
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();

  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0x01));

  // Key update should still not be allowed, since no packet has been acked
  // from the current key phase.
  EXPECT_FALSE(connection_.IsKeyUpdateAllowed());
  EXPECT_FALSE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  // Send packet 1.
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(1u), last_packet);

  // Key update should still not be allowed, even though a packet was sent in
  // the current key phase it hasn't been acked yet.
  EXPECT_FALSE(connection_.IsKeyUpdateAllowed());
  EXPECT_TRUE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  EXPECT_FALSE(connection_.GetDiscardPreviousOneRttKeysAlarm()->IsSet());
  // Receive ack for packet 1.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame1 = InitAckFrame(1);
  ProcessAckPacket(&frame1);

  // OnDecryptedFirstPacketInKeyPhase is called even on the first key phase,
  // so discard_previous_keys_alarm_ should be set now.
  EXPECT_TRUE(connection_.GetDiscardPreviousOneRttKeysAlarm()->IsSet());
  EXPECT_FALSE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  // Key update should now be allowed.
  EXPECT_CALL(visitor_, AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce(
          []() { return std::make_unique<StrictTaggingDecrypter>(0x02); });
  EXPECT_CALL(visitor_, CreateCurrentOneRttEncrypter()).WillOnce([]() {
    return std::make_unique<TaggingEncrypter>(0x02);
  });
  EXPECT_CALL(visitor_, OnKeyUpdate(KeyUpdateReason::kLocalForTests));
  EXPECT_TRUE(connection_.InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));
  // discard_previous_keys_alarm_ should not be set until a packet from the new
  // key phase has been received. (The alarm that was set above should be
  // cleared if it hasn't fired before the next key update happened.)
  EXPECT_FALSE(connection_.GetDiscardPreviousOneRttKeysAlarm()->IsSet());
  EXPECT_FALSE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  // Pretend that peer accepts the key update.
  EXPECT_CALL(peer_framer_visitor_,
              AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce(
          []() { return std::make_unique<StrictTaggingDecrypter>(0x02); });
  EXPECT_CALL(peer_framer_visitor_, CreateCurrentOneRttEncrypter())
      .WillOnce([]() { return std::make_unique<TaggingEncrypter>(0x02); });
  peer_framer_.SetKeyUpdateSupportForConnection(true);
  peer_framer_.DoKeyUpdate(KeyUpdateReason::kRemote);

  // Another key update should not be allowed yet.
  EXPECT_FALSE(connection_.IsKeyUpdateAllowed());

  // Send packet 2.
  SendStreamDataToPeer(2, "bar", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(2u), last_packet);
  EXPECT_TRUE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());
  // Receive ack for packet 2.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame2 = InitAckFrame(2);
  ProcessAckPacket(&frame2);
  EXPECT_TRUE(connection_.GetDiscardPreviousOneRttKeysAlarm()->IsSet());
  EXPECT_FALSE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  // Key update should be allowed again now that a packet has been acked from
  // the current key phase.
  EXPECT_CALL(visitor_, AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce(
          []() { return std::make_unique<StrictTaggingDecrypter>(0x03); });
  EXPECT_CALL(visitor_, CreateCurrentOneRttEncrypter()).WillOnce([]() {
    return std::make_unique<TaggingEncrypter>(0x03);
  });
  EXPECT_CALL(visitor_, OnKeyUpdate(KeyUpdateReason::kLocalForTests));
  EXPECT_TRUE(connection_.InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));

  // Pretend that peer accepts the key update.
  EXPECT_CALL(peer_framer_visitor_,
              AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce(
          []() { return std::make_unique<StrictTaggingDecrypter>(0x03); });
  EXPECT_CALL(peer_framer_visitor_, CreateCurrentOneRttEncrypter())
      .WillOnce([]() { return std::make_unique<TaggingEncrypter>(0x03); });
  peer_framer_.DoKeyUpdate(KeyUpdateReason::kRemote);

  // Another key update should not be allowed yet.
  EXPECT_FALSE(connection_.IsKeyUpdateAllowed());

  // Send packet 3.
  SendStreamDataToPeer(3, "baz", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(3u), last_packet);

  // Another key update should not be allowed yet.
  EXPECT_FALSE(connection_.IsKeyUpdateAllowed());
  EXPECT_TRUE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  // Receive ack for packet 3.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame3 = InitAckFrame(3);
  ProcessAckPacket(&frame3);
  EXPECT_TRUE(connection_.GetDiscardPreviousOneRttKeysAlarm()->IsSet());
  EXPECT_FALSE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());

  // Key update should be allowed now.
  EXPECT_CALL(visitor_, AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce(
          []() { return std::make_unique<StrictTaggingDecrypter>(0x04); });
  EXPECT_CALL(visitor_, CreateCurrentOneRttEncrypter()).WillOnce([]() {
    return std::make_unique<TaggingEncrypter>(0x04);
  });
  EXPECT_CALL(visitor_, OnKeyUpdate(KeyUpdateReason::kLocalForTests));
  EXPECT_TRUE(connection_.InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));
  EXPECT_FALSE(connection_.GetDiscardPreviousOneRttKeysAlarm()->IsSet());
  EXPECT_FALSE(connection_.HaveSentPacketsInCurrentKeyPhaseButNoneAcked());
}

TEST_P(QuicConnectionTest, InitiateKeyUpdateApproachingConfidentialityLimit) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  SetQuicFlag(FLAGS_quic_key_update_confidentiality_limit, 3U);

  std::string error_details;
  TransportParameters params;
  // Key update is enabled.
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  MockFramerVisitor peer_framer_visitor_;
  peer_framer_.set_visitor(&peer_framer_visitor_);

  use_tagging_decrypter();

  uint8_t current_tag = 0x01;

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(current_tag));
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypter>(current_tag));
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();

  peer_framer_.SetKeyUpdateSupportForConnection(true);
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(current_tag));

  const QuicConnectionStats& stats = connection_.GetStats();

  for (int packet_num = 1; packet_num <= 8; ++packet_num) {
    if (packet_num == 3 || packet_num == 6) {
      current_tag++;
      EXPECT_CALL(visitor_, AdvanceKeysAndCreateCurrentOneRttDecrypter())
          .WillOnce([current_tag]() {
            return std::make_unique<StrictTaggingDecrypter>(current_tag);
          });
      EXPECT_CALL(visitor_, CreateCurrentOneRttEncrypter())
          .WillOnce([current_tag]() {
            return std::make_unique<TaggingEncrypter>(current_tag);
          });
      EXPECT_CALL(visitor_,
                  OnKeyUpdate(KeyUpdateReason::kLocalKeyUpdateLimitOverride));
    }
    // Send packet.
    QuicPacketNumber last_packet;
    SendStreamDataToPeer(packet_num, "foo", 0, NO_FIN, &last_packet);
    EXPECT_EQ(QuicPacketNumber(packet_num), last_packet);
    if (packet_num >= 6) {
      EXPECT_EQ(2U, stats.key_update_count);
    } else if (packet_num >= 3) {
      EXPECT_EQ(1U, stats.key_update_count);
    } else {
      EXPECT_EQ(0U, stats.key_update_count);
    }

    if (packet_num == 4 || packet_num == 7) {
      // Pretend that peer accepts the key update.
      EXPECT_CALL(peer_framer_visitor_,
                  AdvanceKeysAndCreateCurrentOneRttDecrypter())
          .WillOnce([current_tag]() {
            return std::make_unique<StrictTaggingDecrypter>(current_tag);
          });
      EXPECT_CALL(peer_framer_visitor_, CreateCurrentOneRttEncrypter())
          .WillOnce([current_tag]() {
            return std::make_unique<TaggingEncrypter>(current_tag);
          });
      peer_framer_.DoKeyUpdate(KeyUpdateReason::kRemote);
    }
    // Receive ack for packet.
    EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
    QuicAckFrame frame1 = InitAckFrame(packet_num);
    ProcessAckPacket(&frame1);
  }
}

TEST_P(QuicConnectionTest,
       CloseConnectionOnConfidentialityLimitKeyUpdateNotAllowed) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  // Set key update confidentiality limit to 1 packet.
  SetQuicFlag(FLAGS_quic_key_update_confidentiality_limit, 1U);
  // Use confidentiality limit for connection close of 3 packets.
  constexpr size_t kConfidentialityLimit = 3U;

  std::string error_details;
  TransportParameters params;
  // Key update is enabled.
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(
      ENCRYPTION_FORWARD_SECURE,
      std::make_unique<NullEncrypterWithConfidentialityLimit>(
          Perspective::IS_CLIENT, kConfidentialityLimit));
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();

  QuicPacketNumber last_packet;
  // Send 3 packets without receiving acks for any of them. Key update will not
  // be allowed, so the confidentiality limit should be reached, forcing the
  // connection to be closed.
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.connected());
  SendStreamDataToPeer(2, "foo", 0, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  SendStreamDataToPeer(3, "foo", 0, NO_FIN, &last_packet);
  EXPECT_FALSE(connection_.connected());
  const QuicConnectionStats& stats = connection_.GetStats();
  EXPECT_EQ(0U, stats.key_update_count);
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest,
       CloseConnectionOnConfidentialityLimitKeyUpdateNotSupportedByPeer) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  // Set key update confidentiality limit to 1 packet.
  SetQuicFlag(FLAGS_quic_key_update_confidentiality_limit, 1U);
  // Use confidentiality limit for connection close of 3 packets.
  constexpr size_t kConfidentialityLimit = 3U;

  std::string error_details;
  TransportParameters params;
  // Key update not enabled for this connection as peer doesn't support it.
  params.key_update_not_yet_supported = true;
  QuicConfig config;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  // Key update is supported locally.
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(
      ENCRYPTION_FORWARD_SECURE,
      std::make_unique<NullEncrypterWithConfidentialityLimit>(
          Perspective::IS_CLIENT, kConfidentialityLimit));
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();

  QuicPacketNumber last_packet;
  // Send 3 packets and receive acks for them. Since key update is not enabled
  // the confidentiality limit should be reached, forcing the connection to be
  // closed.
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.connected());
  // Receive ack for packet.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame1 = InitAckFrame(1);
  ProcessAckPacket(&frame1);

  SendStreamDataToPeer(2, "foo", 0, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.connected());
  // Receive ack for packet.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame2 = InitAckFrame(2);
  ProcessAckPacket(&frame2);

  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  SendStreamDataToPeer(3, "foo", 0, NO_FIN, &last_packet);
  EXPECT_FALSE(connection_.connected());
  const QuicConnectionStats& stats = connection_.GetStats();
  EXPECT_EQ(0U, stats.key_update_count);
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest,
       CloseConnectionOnConfidentialityLimitKeyUpdateNotEnabledLocally) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  // Set key update confidentiality limit to 1 packet.
  SetQuicFlag(FLAGS_quic_key_update_confidentiality_limit, 1U);
  // Use confidentiality limit for connection close of 3 packets.
  constexpr size_t kConfidentialityLimit = 3U;

  std::string error_details;
  TransportParameters params;
  // Key update is supported by peer but not locally
  // (config.SetKeyUpdateSupportedLocally is not called.)
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(
      ENCRYPTION_FORWARD_SECURE,
      std::make_unique<NullEncrypterWithConfidentialityLimit>(
          Perspective::IS_CLIENT, kConfidentialityLimit));
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();

  QuicPacketNumber last_packet;
  // Send 3 packets and receive acks for them. Since key update is not enabled
  // the confidentiality limit should be reached, forcing the connection to be
  // closed.
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.connected());
  // Receive ack for packet.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame1 = InitAckFrame(1);
  ProcessAckPacket(&frame1);

  SendStreamDataToPeer(2, "foo", 0, NO_FIN, &last_packet);
  EXPECT_TRUE(connection_.connected());
  // Receive ack for packet.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame2 = InitAckFrame(2);
  ProcessAckPacket(&frame2);

  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  SendStreamDataToPeer(3, "foo", 0, NO_FIN, &last_packet);
  EXPECT_FALSE(connection_.connected());
  const QuicConnectionStats& stats = connection_.GetStats();
  EXPECT_EQ(0U, stats.key_update_count);
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest, CloseConnectionOnIntegrityLimitDuringHandshake) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  constexpr uint8_t correct_tag = 0x01;
  constexpr uint8_t wrong_tag = 0xFE;
  constexpr QuicPacketCount kIntegrityLimit = 3;

  SetDecrypter(ENCRYPTION_HANDSHAKE,
               std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
                   correct_tag, kIntegrityLimit));
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(correct_tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(wrong_tag));
  for (uint64_t i = 1; i <= kIntegrityLimit; ++i) {
    EXPECT_TRUE(connection_.connected());
    if (i == kIntegrityLimit) {
      EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
      EXPECT_CALL(visitor_, OnHandshakePacketSent()).Times(AnyNumber());
    }
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_HANDSHAKE);
    EXPECT_EQ(
        i, connection_.GetStats().num_failed_authentication_packets_received);
  }
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest, CloseConnectionOnIntegrityLimitAfterHandshake) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  constexpr uint8_t correct_tag = 0x01;
  constexpr uint8_t wrong_tag = 0xFE;
  constexpr QuicPacketCount kIntegrityLimit = 3;

  use_tagging_decrypter();
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
                   correct_tag, kIntegrityLimit));
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(correct_tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();
  connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(wrong_tag));
  for (uint64_t i = 1; i <= kIntegrityLimit; ++i) {
    EXPECT_TRUE(connection_.connected());
    if (i == kIntegrityLimit) {
      EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
    }
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
    EXPECT_EQ(
        i, connection_.GetStats().num_failed_authentication_packets_received);
  }
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest,
       CloseConnectionOnIntegrityLimitAcrossEncryptionLevels) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  constexpr uint8_t correct_tag = 0x01;
  constexpr uint8_t wrong_tag = 0xFE;
  constexpr QuicPacketCount kIntegrityLimit = 4;

  use_tagging_decrypter();
  SetDecrypter(ENCRYPTION_HANDSHAKE,
               std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
                   correct_tag, kIntegrityLimit));
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(correct_tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(wrong_tag));
  for (uint64_t i = 1; i <= 2; ++i) {
    EXPECT_TRUE(connection_.connected());
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_HANDSHAKE);
    EXPECT_EQ(
        i, connection_.GetStats().num_failed_authentication_packets_received);
  }

  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
                   correct_tag, kIntegrityLimit));
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(correct_tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();
  connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
  connection_.RemoveEncrypter(ENCRYPTION_HANDSHAKE);
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(wrong_tag));
  for (uint64_t i = 3; i <= kIntegrityLimit; ++i) {
    EXPECT_TRUE(connection_.connected());
    if (i == kIntegrityLimit) {
      EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
    }
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
    EXPECT_EQ(
        i, connection_.GetStats().num_failed_authentication_packets_received);
  }
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest, IntegrityLimitDoesNotApplyWithoutDecryptionKey) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  constexpr uint8_t correct_tag = 0x01;
  constexpr uint8_t wrong_tag = 0xFE;
  constexpr QuicPacketCount kIntegrityLimit = 3;

  use_tagging_decrypter();
  SetDecrypter(ENCRYPTION_HANDSHAKE,
               std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
                   correct_tag, kIntegrityLimit));
  connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                           std::make_unique<TaggingEncrypter>(correct_tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
  connection_.RemoveDecrypter(ENCRYPTION_FORWARD_SECURE);

  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(wrong_tag));
  for (uint64_t i = 1; i <= kIntegrityLimit * 2; ++i) {
    EXPECT_TRUE(connection_.connected());
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
    EXPECT_EQ(
        0u, connection_.GetStats().num_failed_authentication_packets_received);
  }
  EXPECT_TRUE(connection_.connected());
}

TEST_P(QuicConnectionTest, CloseConnectionOnIntegrityLimitAcrossKeyPhases) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  constexpr QuicPacketCount kIntegrityLimit = 4;

  TransportParameters params;
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  std::string error_details;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  if (connection_.version().UsesTls()) {
    QuicConfigPeer::SetReceivedOriginalConnectionId(
        &config, connection_.connection_id());
    QuicConfigPeer::SetReceivedInitialSourceConnectionId(
        &config, connection_.connection_id());
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  MockFramerVisitor peer_framer_visitor_;
  peer_framer_.set_visitor(&peer_framer_visitor_);

  use_tagging_decrypter();
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           std::make_unique<TaggingEncrypter>(0x01));
  SetDecrypter(ENCRYPTION_FORWARD_SECURE,
               std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
                   0x01, kIntegrityLimit));
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_CONFIRMED));
  connection_.OnHandshakeComplete();
  connection_.RemoveEncrypter(ENCRYPTION_INITIAL);

  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0xFF));
  for (uint64_t i = 1; i <= 2; ++i) {
    EXPECT_TRUE(connection_.connected());
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
    EXPECT_EQ(
        i, connection_.GetStats().num_failed_authentication_packets_received);
  }

  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0x01));
  // Send packet 1.
  QuicPacketNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(1u), last_packet);
  // Receive ack for packet 1.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame1 = InitAckFrame(1);
  ProcessAckPacket(&frame1);
  // Key update should now be allowed, initiate it.
  EXPECT_CALL(visitor_, AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce([kIntegrityLimit]() {
        return std::make_unique<StrictTaggingDecrypterWithIntegrityLimit>(
            0x02, kIntegrityLimit);
      });
  EXPECT_CALL(visitor_, CreateCurrentOneRttEncrypter()).WillOnce([]() {
    return std::make_unique<TaggingEncrypter>(0x02);
  });
  EXPECT_CALL(visitor_, OnKeyUpdate(KeyUpdateReason::kLocalForTests));
  EXPECT_TRUE(connection_.InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));

  // Pretend that peer accepts the key update.
  EXPECT_CALL(peer_framer_visitor_,
              AdvanceKeysAndCreateCurrentOneRttDecrypter())
      .WillOnce(
          []() { return std::make_unique<StrictTaggingDecrypter>(0x02); });
  EXPECT_CALL(peer_framer_visitor_, CreateCurrentOneRttEncrypter())
      .WillOnce([]() { return std::make_unique<TaggingEncrypter>(0x02); });
  peer_framer_.SetKeyUpdateSupportForConnection(true);
  peer_framer_.DoKeyUpdate(KeyUpdateReason::kLocalForTests);

  // Send packet 2.
  SendStreamDataToPeer(2, "bar", 0, NO_FIN, &last_packet);
  EXPECT_EQ(QuicPacketNumber(2u), last_packet);
  // Receive ack for packet 2.
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(true, _, _, _, _));
  QuicAckFrame frame2 = InitAckFrame(2);
  ProcessAckPacket(&frame2);

  EXPECT_EQ(2u,
            connection_.GetStats().num_failed_authentication_packets_received);

  // Do two more undecryptable packets. Integrity limit should be reached.
  peer_framer_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<TaggingEncrypter>(0xFF));
  for (uint64_t i = 3; i <= kIntegrityLimit; ++i) {
    EXPECT_TRUE(connection_.connected());
    if (i == kIntegrityLimit) {
      EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
    }
    ProcessDataPacketAtLevel(i, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
    EXPECT_EQ(
        i, connection_.GetStats().num_failed_authentication_packets_received);
  }
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(QUIC_AEAD_LIMIT_REACHED);
}

TEST_P(QuicConnectionTest, SendAckFrequencyFrame) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  SetQuicReloadableFlag(quic_can_send_ack_frequency, true);
  set_perspective(Perspective::IS_SERVER);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());

  QuicConfig config;
  QuicConfigPeer::SetReceivedMinAckDelayMs(&config, /*min_ack_delay_ms=*/1);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  QuicConnectionPeer::SetAddressValidated(&connection_);
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  peer_creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);

  connection_.OnHandshakeComplete();

  writer_->SetWritable();
  QuicPacketCreatorPeer::SetPacketNumber(creator_, 99);
  // Send packet 100
  SendStreamDataToPeer(/*id=*/1, "foo", /*offset=*/0, NO_FIN, nullptr);

  QuicAckFrequencyFrame captured_frame;
  EXPECT_CALL(visitor_, SendAckFrequency(_))
      .WillOnce(Invoke([&captured_frame](const QuicAckFrequencyFrame& frame) {
        captured_frame = frame;
      }));
  // Send packet 101.
  SendStreamDataToPeer(/*id=*/1, "bar", /*offset=*/3, NO_FIN, nullptr);

  EXPECT_EQ(captured_frame.packet_tolerance, 10u);
  EXPECT_EQ(captured_frame.max_ack_delay,
            QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs));

  // Sending packet 102 does not trigger sending another AckFrequencyFrame.
  SendStreamDataToPeer(/*id=*/1, "baz", /*offset=*/6, NO_FIN, nullptr);
}

TEST_P(QuicConnectionTest, SendAckFrequencyFrameUponHandshakeCompletion) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  SetQuicReloadableFlag(quic_can_send_ack_frequency, true);
  set_perspective(Perspective::IS_SERVER);
  EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(AnyNumber());

  QuicConfig config;
  QuicConfigPeer::SetReceivedMinAckDelayMs(&config, /*min_ack_delay_ms=*/1);
  QuicTagVector quic_tag_vector;
  // Enable sending AckFrequency upon handshake completion.
  quic_tag_vector.push_back(kAFF2);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, quic_tag_vector);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  QuicConnectionPeer::SetAddressValidated(&connection_);
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  peer_creator_.set_encryption_level(ENCRYPTION_FORWARD_SECURE);

  QuicAckFrequencyFrame captured_frame;
  EXPECT_CALL(visitor_, SendAckFrequency(_))
      .WillOnce(Invoke([&captured_frame](const QuicAckFrequencyFrame& frame) {
        captured_frame = frame;
      }));

  connection_.OnHandshakeComplete();

  EXPECT_EQ(captured_frame.packet_tolerance, 2u);
  EXPECT_EQ(captured_frame.max_ack_delay,
            QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs));
}

TEST_P(QuicConnectionTest, FastRecoveryOfLostServerHello) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);

  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  connection_.SendCryptoStreamData();
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(20));

  // Assume ServerHello gets lost.
  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(0x02));
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_HANDSHAKE);
  ASSERT_TRUE(connection_.GetRetransmissionAlarm()->IsSet());
  // Shorten PTO for fast recovery from lost ServerHello.
  EXPECT_EQ(clock_.ApproximateNow() + kAlarmGranularity,
            connection_.GetRetransmissionAlarm()->deadline());
}

TEST_P(QuicConnectionTest, ServerHelloGetsReordered) {
  if (!connection_.SupportsMultiplePacketNumberSpaces()) {
    return;
  }
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  QuicConfig config;
  connection_.SetFromConfig(config);
  EXPECT_CALL(visitor_, OnCryptoFrame(_))
      .WillRepeatedly(Invoke([=](const QuicCryptoFrame& frame) {
        if (frame.level == ENCRYPTION_INITIAL) {
          // Install handshake read keys.
          SetDecrypter(ENCRYPTION_HANDSHAKE,
                       std::make_unique<StrictTaggingDecrypter>(0x02));
          connection_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                                   std::make_unique<TaggingEncrypter>(0x02));
          connection_.SetDefaultEncryptionLevel(ENCRYPTION_HANDSHAKE);
        }
      }));

  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_INITIAL,
                           std::make_unique<TaggingEncrypter>(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  connection_.SendCryptoStreamData();
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(20));

  // Assume ServerHello gets reordered.
  peer_framer_.SetEncrypter(ENCRYPTION_HANDSHAKE,
                            std::make_unique<TaggingEncrypter>(0x02));
  ProcessCryptoPacketAtLevel(2, ENCRYPTION_HANDSHAKE);
  ProcessCryptoPacketAtLevel(1, ENCRYPTION_INITIAL);
  // Verify fast recovery is not enabled.
  EXPECT_EQ(connection_.sent_packet_manager().GetRetransmissionTime(),
            connection_.GetRetransmissionAlarm()->deadline());
}

TEST_P(QuicConnectionTest, MigratePath) {
  EXPECT_CALL(visitor_, OnPathDegrading());
  connection_.OnPathDegradingDetected();
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(visitor_, OnForwardProgressMadeAfterPathDegrading());
  connection_.MigratePath(kNewSelfAddress, connection_.peer_address(),
                          &new_writer, /*owns_writer=*/false);
  EXPECT_EQ(kNewSelfAddress, connection_.self_address());
  EXPECT_EQ(&new_writer, QuicConnectionPeer::GetWriter(&connection_));
  EXPECT_FALSE(connection_.IsPathDegrading());
}

TEST_P(QuicConnectionTest, MigrateToNewPathDuringProbing) {
  if (!VersionHasIetfQuicFrames(connection_.version().transport_version) ||
      !connection_.use_path_validator()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_CLIENT);
  const QuicSocketAddress kNewSelfAddress(QuicIpAddress::Any4(), 12345);
  EXPECT_NE(kNewSelfAddress, connection_.self_address());
  TestPacketWriter new_writer(version(), &clock_, Perspective::IS_CLIENT);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  bool success = false;
  connection_.ValidatePath(
      std::make_unique<TestQuicPathValidationContext>(
          kNewSelfAddress, connection_.peer_address(), &new_writer),
      std::make_unique<TestValidationResultDelegate>(
          kNewSelfAddress, connection_.peer_address(), &success));
  EXPECT_TRUE(connection_.HasPendingPathValidation());
  EXPECT_TRUE(QuicConnectionPeer::IsAlternativePath(
      &connection_, kNewSelfAddress, connection_.peer_address()));

  connection_.MigratePath(kNewSelfAddress, connection_.peer_address(),
                          &new_writer, /*owns_writer=*/false);
  EXPECT_EQ(kNewSelfAddress, connection_.self_address());
  EXPECT_TRUE(connection_.HasPendingPathValidation());
  EXPECT_FALSE(QuicConnectionPeer::IsAlternativePath(
      &connection_, kNewSelfAddress, connection_.peer_address()));
}

TEST_P(QuicConnectionTest, SingleAckInPacket) {
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.RemoveEncrypter(ENCRYPTION_INITIAL);
  connection_.NeuterUnencryptedPackets();
  connection_.OnHandshakeComplete();

  EXPECT_CALL(visitor_, OnStreamFrame(_)).WillOnce(Invoke([=]() {
    connection_.SendStreamData3();
    connection_.CloseConnection(
        QUIC_INTERNAL_ERROR, "error",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }));
  QuicFrames frames;
  frames.push_back(QuicFrame(frame1_));
  ProcessFramesPacketWithAddresses(frames, kSelfAddress, kPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  ASSERT_FALSE(writer_->ack_frames().empty());
  if (GetQuicReloadableFlag(quic_single_ack_in_packet2)) {
    EXPECT_EQ(1u, writer_->ack_frames().size());
  } else {
    EXPECT_EQ(2u, writer_->ack_frames().size());
  }
}

TEST_P(QuicConnectionTest,
       ServerReceivedZeroRttPacketAfterOneRttPacketWithRetainedKey) {
  if (!connection_.version().UsesTls()) {
    return;
  }

  set_perspective(Perspective::IS_SERVER);
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<NullDecrypter>(Perspective::IS_SERVER));

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);

  // Finish handshake.
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  notifier_.NeuterUnencryptedData();
  connection_.NeuterUnencryptedPackets();
  connection_.OnHandshakeComplete();
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(4, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(connection_.GetDiscardZeroRttDecryptionKeysAlarm()->IsSet());

  // 0-RTT packet received out of order should be decoded since the decrypter
  // is temporarily retained.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(2, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  EXPECT_EQ(
      0u,
      connection_.GetStats()
          .num_tls_server_zero_rtt_packets_received_after_discarding_decrypter);

  // Simulate the timeout for discarding 0-RTT keys passing.
  connection_.GetDiscardZeroRttDecryptionKeysAlarm()->Fire();

  // Another 0-RTT packet received now should not be decoded.
  EXPECT_FALSE(connection_.GetDiscardZeroRttDecryptionKeysAlarm()->IsSet());
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(0);
  ProcessDataPacketAtLevel(3, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  EXPECT_EQ(
      1u,
      connection_.GetStats()
          .num_tls_server_zero_rtt_packets_received_after_discarding_decrypter);

  // The |discard_zero_rtt_decryption_keys_alarm_| should only be set on the
  // first 1-RTT packet received.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(5, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  EXPECT_FALSE(connection_.GetDiscardZeroRttDecryptionKeysAlarm()->IsSet());
}

TEST_P(QuicConnectionTest, NewTokenFrameInstigateAcks) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  SetQuicReloadableFlag(quic_enable_token_based_address_validation, true);
  EXPECT_CALL(visitor_, OnSuccessfulVersionNegotiation(_));

  QuicNewTokenFrame* new_token = new QuicNewTokenFrame();
  EXPECT_CALL(visitor_, OnNewTokenReceived(_));
  ProcessFramePacket(QuicFrame(new_token));

  // Ensure that this has caused the ACK alarm to be set.
  EXPECT_TRUE(connection_.HasPendingAcks());
}

TEST_P(QuicConnectionTest, ServerClosesConnectionOnNewTokenFrame) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  SetQuicReloadableFlag(quic_enable_token_based_address_validation, true);
  set_perspective(Perspective::IS_SERVER);
  QuicNewTokenFrame* new_token = new QuicNewTokenFrame();
  EXPECT_CALL(visitor_, OnNewTokenReceived(_)).Times(0);
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  ProcessFramePacket(QuicFrame(new_token));
  EXPECT_FALSE(connection_.connected());
}

TEST_P(QuicConnectionTest, OverrideRetryTokenWithRetryPacket) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  std::string address_token = "TestAddressToken";
  connection_.SetSourceAddressTokenToSend(address_token);
  EXPECT_EQ(QuicPacketCreatorPeer::GetRetryToken(
                QuicConnectionPeer::GetPacketCreator(&connection_)),
            address_token);
  // Passes valid retry and verify token gets overridden.
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);
}

TEST_P(QuicConnectionTest, DonotOverrideRetryTokenWithAddressToken) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  // Passes valid retry and verify token gets overridden.
  TestClientRetryHandling(/*invalid_retry_tag=*/false,
                          /*missing_original_id_in_config=*/false,
                          /*wrong_original_id_in_config=*/false,
                          /*missing_retry_id_in_config=*/false,
                          /*wrong_retry_id_in_config=*/false);
  std::string retry_token = QuicPacketCreatorPeer::GetRetryToken(
      QuicConnectionPeer::GetPacketCreator(&connection_));

  std::string address_token = "TestAddressToken";
  connection_.SetSourceAddressTokenToSend(address_token);
  EXPECT_EQ(QuicPacketCreatorPeer::GetRetryToken(
                QuicConnectionPeer::GetPacketCreator(&connection_)),
            retry_token);
}

TEST_P(QuicConnectionTest,
       ServerReceivedZeroRttWithHigherPacketNumberThanOneRttAndFlagDisabled) {
  SetQuicReloadableFlag(
      quic_close_connection_on_0rtt_packet_number_higher_than_1rtt, false);
  if (!connection_.version().UsesTls()) {
    return;
  }

  // The code that checks for this error piggybacks on some book-keeping state
  // kept for key update, so enable key update for the test.
  std::string error_details;
  TransportParameters params;
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  QuicConfigPeer::SetReceivedOriginalConnectionId(&config,
                                                  connection_.connection_id());
  QuicConfigPeer::SetReceivedInitialSourceConnectionId(
      &config, connection_.connection_id());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  set_perspective(Perspective::IS_SERVER);
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<NullDecrypter>(Perspective::IS_SERVER));

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);

  // Finish handshake.
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  notifier_.NeuterUnencryptedData();
  connection_.NeuterUnencryptedPackets();
  connection_.OnHandshakeComplete();
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));

  // Decrypt a 1-RTT packet.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(2, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(connection_.GetDiscardZeroRttDecryptionKeysAlarm()->IsSet());

  // 0-RTT packet with higher packet number than a 1-RTT packet is invalid, but
  // accepted as the
  // quic_close_connection_on_0rtt_packet_number_higher_than_1rtt
  // flag is disabled.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(3, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  EXPECT_TRUE(connection_.connected());
}

TEST_P(QuicConnectionTest,
       ServerReceivedZeroRttWithHigherPacketNumberThanOneRtt) {
  SetQuicReloadableFlag(
      quic_close_connection_on_0rtt_packet_number_higher_than_1rtt, true);
  if (!connection_.version().UsesTls()) {
    return;
  }

  // The code that checks for this error piggybacks on some book-keeping state
  // kept for key update, so enable key update for the test.
  std::string error_details;
  TransportParameters params;
  params.key_update_not_yet_supported = false;
  QuicConfig config;
  EXPECT_THAT(config.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  config.SetKeyUpdateSupportedLocally();
  QuicConfigPeer::SetNegotiated(&config, true);
  QuicConfigPeer::SetReceivedOriginalConnectionId(&config,
                                                  connection_.connection_id());
  QuicConfigPeer::SetReceivedInitialSourceConnectionId(
      &config, connection_.connection_id());
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);

  set_perspective(Perspective::IS_SERVER);
  SetDecrypter(ENCRYPTION_ZERO_RTT,
               std::make_unique<NullDecrypter>(Perspective::IS_SERVER));

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);

  // Finish handshake.
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  notifier_.NeuterUnencryptedData();
  connection_.NeuterUnencryptedPackets();
  connection_.OnHandshakeComplete();
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_COMPLETE));

  // Decrypt a 1-RTT packet.
  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(2, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(connection_.GetDiscardZeroRttDecryptionKeysAlarm()->IsSet());

  // 0-RTT packet with higher packet number than a 1-RTT packet is invalid and
  // should cause the connection to be closed.
  EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  EXPECT_CALL(visitor_, OnConnectionClosed(_, _));
  ProcessDataPacketAtLevel(3, !kHasStopWaiting, ENCRYPTION_ZERO_RTT);
  EXPECT_FALSE(connection_.connected());
  TestConnectionCloseQuicErrorCode(
      QUIC_INVALID_0RTT_PACKET_NUMBER_OUT_OF_ORDER);
}

// Regression test for b/177312785
TEST_P(QuicConnectionTest, PeerMigrateBeforeHandshakeConfirm) {
  if (!VersionHasIetfQuicFrames(version().transport_version) ||
      !GetQuicReloadableFlag(quic_start_peer_migration_earlier)) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  QuicPacketCreatorPeer::SetSendVersionInPacket(creator_, false);
  EXPECT_EQ(Perspective::IS_SERVER, connection_.perspective());
  EXPECT_CALL(visitor_, GetHandshakeState())
      .WillRepeatedly(Return(HANDSHAKE_START));

  // Clear direct_peer_address.
  QuicConnectionPeer::SetDirectPeerAddress(&connection_, QuicSocketAddress());
  // Clear effective_peer_address, it is the same as direct_peer_address for
  // this test.
  QuicConnectionPeer::SetEffectivePeerAddress(&connection_,
                                              QuicSocketAddress());
  EXPECT_FALSE(connection_.effective_peer_address().IsInitialized());

  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback6(), /*port=*/23456);
  EXPECT_CALL(visitor_, OnCryptoFrame(_)).Times(AnyNumber());
  ProcessFramePacketWithAddresses(MakeCryptoFrame(), kSelfAddress, kPeerAddress,
                                  ENCRYPTION_INITIAL);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());

  // Process another packet with a different peer address on server side will
  // close connection.
  QuicAckFrame frame = InitAckFrame(1);
  EXPECT_CALL(visitor_, BeforeConnectionCloseSent());
  EXPECT_CALL(visitor_,
              OnConnectionClosed(_, ConnectionCloseSource::FROM_SELF));
  EXPECT_CALL(visitor_, OnConnectionMigration(PORT_CHANGE)).Times(0u);
  if (!GetQuicReloadableFlag(quic_update_packet_content_returns_connected)) {
    EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _));
    EXPECT_QUIC_BUG(
        ProcessFramePacketWithAddresses(QuicFrame(&frame), kSelfAddress,
                                        kNewPeerAddress, ENCRYPTION_INITIAL),
        "");
  } else {
    EXPECT_CALL(*send_algorithm_, OnCongestionEvent(_, _, _, _, _)).Times(0);
    ProcessFramePacketWithAddresses(QuicFrame(&frame), kSelfAddress,
                                    kNewPeerAddress, ENCRYPTION_INITIAL);
  }
  EXPECT_FALSE(connection_.connected());
}

// Regresstion test for b/175685916
TEST_P(QuicConnectionTest, TryToFlushAckWithAckQueued) {
  if (!version().HasIetfQuicFrames()) {
    return;
  }
  SetQuicReloadableFlag(quic_can_send_ack_frequency, true);
  SetQuicReloadableFlag(quic_single_ack_in_packet2, true);
  set_perspective(Perspective::IS_SERVER);

  QuicConfig config;
  QuicConfigPeer::SetReceivedMinAckDelayMs(&config, /*min_ack_delay_ms=*/1);
  EXPECT_CALL(*send_algorithm_, SetFromConfig(_, _));
  connection_.SetFromConfig(config);
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_.OnHandshakeComplete();
  QuicPacketCreatorPeer::SetPacketNumber(creator_, 200);

  EXPECT_CALL(visitor_, OnStreamFrame(_)).Times(1);
  ProcessDataPacketAtLevel(1, !kHasStopWaiting, ENCRYPTION_FORWARD_SECURE);
  // Sending ACK_FREQUENCY bundles ACK. QuicConnectionPeer::SendPing
  // will try to bundle ACK but there is no pending ACK.
  EXPECT_CALL(visitor_, SendAckFrequency(_))
      .WillOnce(Invoke(&notifier_,
                       &SimpleSessionNotifier::WriteOrBufferAckFrequency));
  QuicConnectionPeer::SendPing(&connection_);
}

TEST_P(QuicConnectionTest, PathChallengeBeforePeerIpAddressChangeAtServer) {
  if (!connection_.validate_client_address()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  PathProbeTestInit(Perspective::IS_SERVER);

  const QuicSocketAddress kNewPeerAddress =
      QuicSocketAddress(QuicIpAddress::Loopback4(), /*port=*/23456);
  QuicPathFrameBuffer path_challenge_payload{0, 1, 2, 3, 4, 5, 6, 7};
  QuicFrames frames1;
  frames1.push_back(
      QuicFrame(new QuicPathChallengeFrame(0, path_challenge_payload)));
  QuicPathFrameBuffer payload;
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, NO_RETRANSMITTABLE_DATA))
      .Times(AtLeast(1))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
        EXPECT_EQ(kPeerAddress, connection_.peer_address());
        EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
        EXPECT_FALSE(writer_->path_response_frames().empty());
        EXPECT_FALSE(writer_->path_challenge_frames().empty());
        payload = writer_->path_challenge_frames().front().data_buffer;
      }));
  ProcessFramesPacketWithAddresses(frames1, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kPeerAddress, connection_.peer_address());
  EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
  EXPECT_TRUE(connection_.HasPendingPathValidation());

  // Process another packet with a different peer address on server side will
  // start connection migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE)).Times(1);
  EXPECT_CALL(visitor_, OnStreamFrame(_)).WillOnce(Invoke([=]() {
    EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  }));
  // IETF QUIC send algorithm should be changed to a different object, so no
  // OnPacketSent() called on the old send algorithm.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, NO_RETRANSMITTABLE_DATA))
      .Times(0);
  QuicFrames frames2;
  frames2.push_back(QuicFrame(frame2_));
  ProcessFramesPacketWithAddresses(frames2, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());
  EXPECT_TRUE(writer_->path_challenge_frames().empty());
  EXPECT_NE(connection_.sent_packet_manager().GetSendAlgorithm(),
            send_algorithm_);
  // Switch to use the mock send algorithm.
  send_algorithm_ = new StrictMock<MockSendAlgorithm>();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
      .Times(AnyNumber())
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, InSlowStart()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, InRecovery()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, PopulateConnectionStats(_)).Times(AnyNumber());
  connection_.SetSendAlgorithm(send_algorithm_);

  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            connection_.active_effective_peer_migration_type());
  EXPECT_EQ(1u, connection_.GetStats()
                    .num_peer_migration_to_proactively_validated_address);

  // The PATH_CHALLENGE and PATH_RESPONSE is expanded upto the max packet size
  // which may exceeds the anti-amplification limit. Verify server is throttled
  // by anti-amplification limit.
  connection_.SendCryptoDataWithString("foo", 0);
  EXPECT_FALSE(connection_.GetRetransmissionAlarm()->IsSet());

  // Receiving PATH_RESPONSE should lift the anti-amplification limit.
  QuicFrames frames3;
  frames3.push_back(QuicFrame(new QuicPathResponseFrame(99, payload)));
  EXPECT_CALL(visitor_, MaybeSendAddressToken());
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .Times(testing::AtLeast(1u));
  ProcessFramesPacketWithAddresses(frames3, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());

  // Verify the anti-amplification limit is lifted by sending a packet larger
  // than the anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _)).Times(1);
  connection_.SendCryptoDataWithString(std::string(1200, 'a'), 0);
  EXPECT_EQ(1u, connection_.GetStats().num_validated_peer_migration);
}

TEST_P(QuicConnectionTest,
       PathValidationSucceedsBeforePeerIpAddressChangeAtServer) {
  if (!connection_.validate_client_address()) {
    return;
  }
  set_perspective(Perspective::IS_SERVER);
  PathProbeTestInit(Perspective::IS_SERVER);

  // Receive probing packet with new peer address.
  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback4(),
                                          /*port=*/23456);
  QuicPathFrameBuffer payload;
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, NO_RETRANSMITTABLE_DATA))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(kNewPeerAddress, writer_->last_write_peer_address());
        EXPECT_EQ(kPeerAddress, connection_.peer_address());
        EXPECT_EQ(kPeerAddress, connection_.effective_peer_address());
        EXPECT_FALSE(writer_->path_response_frames().empty());
        EXPECT_FALSE(writer_->path_challenge_frames().empty());
        payload = writer_->path_challenge_frames().front().data_buffer;
      }))
      .WillRepeatedly(Invoke([&]() {
        // Only start reverse path validation once.
        EXPECT_TRUE(writer_->path_challenge_frames().empty());
      }));
  QuicPathFrameBuffer path_challenge_payload{0, 1, 2, 3, 4, 5, 6, 7};
  QuicFrames frames1;
  frames1.push_back(
      QuicFrame(new QuicPathChallengeFrame(0, path_challenge_payload)));
  ProcessFramesPacketWithAddresses(frames1, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(connection_.HasPendingPathValidation());

  // Receive PATH_RESPONSE should mark the new peer address validated.
  QuicFrames frames3;
  frames3.push_back(QuicFrame(new QuicPathResponseFrame(99, payload)));
  ProcessFramesPacketWithAddresses(frames3, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);

  // Process another packet with a newer peer address with the same port will
  // start connection migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE)).Times(1);
  // IETF QUIC send algorithm should be changed to a different object, so no
  // OnPacketSent() called on the old send algorithm.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, NO_RETRANSMITTABLE_DATA))
      .Times(0);
  const QuicSocketAddress kNewerPeerAddress(QuicIpAddress::Loopback4(),
                                            /*port=*/34567);
  EXPECT_CALL(visitor_, OnStreamFrame(_)).WillOnce(Invoke([=]() {
    EXPECT_EQ(kNewerPeerAddress, connection_.peer_address());
  }));
  EXPECT_CALL(visitor_, MaybeSendAddressToken());
  QuicFrames frames2;
  frames2.push_back(QuicFrame(frame2_));
  ProcessFramesPacketWithAddresses(frames2, kSelfAddress, kNewerPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewerPeerAddress, connection_.peer_address());
  EXPECT_EQ(kNewerPeerAddress, connection_.effective_peer_address());
  // Since the newer address has the same IP as the previously validated probing
  // address. The peer migration becomes validated immediately.
  EXPECT_EQ(NO_CHANGE, connection_.active_effective_peer_migration_type());
  EXPECT_EQ(kNewerPeerAddress, writer_->last_write_peer_address());
  EXPECT_EQ(1u, connection_.GetStats()
                    .num_peer_migration_to_proactively_validated_address);
  EXPECT_FALSE(connection_.HasPendingPathValidation());
  EXPECT_NE(connection_.sent_packet_manager().GetSendAlgorithm(),
            send_algorithm_);

  // Switch to use the mock send algorithm.
  send_algorithm_ = new StrictMock<MockSendAlgorithm>();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
      .Times(AnyNumber())
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, InSlowStart()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, InRecovery()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, PopulateConnectionStats(_)).Times(AnyNumber());
  connection_.SetSendAlgorithm(send_algorithm_);

  // Verify the server is not throttled by the anti-amplification limit by
  // sending a packet larger than the anti-amplification limit.
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _));
  connection_.SendCryptoDataWithString(std::string(1200, 'a'), 0);
  EXPECT_EQ(1u, connection_.GetStats().num_validated_peer_migration);
}

TEST_P(QuicConnectionTest,
       ProbedOnAnotherPathAfterPeerIpAddressChangeAtServer) {
  if (!connection_.validate_client_address()) {
    return;
  }
  PathProbeTestInit(Perspective::IS_SERVER);

  const QuicSocketAddress kNewPeerAddress(QuicIpAddress::Loopback4(),
                                          /*port=*/23456);

  // Process a packet with a new peer address will start connection migration.
  EXPECT_CALL(visitor_, OnConnectionMigration(IPV6_TO_IPV4_CHANGE)).Times(1);
  // IETF QUIC send algorithm should be changed to a different object, so no
  // OnPacketSent() called on the old send algorithm.
  EXPECT_CALL(*send_algorithm_,
              OnPacketSent(_, _, _, _, NO_RETRANSMITTABLE_DATA))
      .Times(0);
  EXPECT_CALL(visitor_, OnStreamFrame(_)).WillOnce(Invoke([=]() {
    EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  }));
  QuicFrames frames2;
  frames2.push_back(QuicFrame(frame2_));
  ProcessFramesPacketWithAddresses(frames2, kSelfAddress, kNewPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_TRUE(QuicConnectionPeer::IsAlternativePathValidated(&connection_));
  EXPECT_TRUE(connection_.HasPendingPathValidation());

  // Switch to use the mock send algorithm.
  send_algorithm_ = new StrictMock<MockSendAlgorithm>();
  EXPECT_CALL(*send_algorithm_, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm_, GetCongestionWindow())
      .WillRepeatedly(Return(kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm_, OnApplicationLimited(_)).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, BandwidthEstimate())
      .Times(AnyNumber())
      .WillRepeatedly(Return(QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm_, InSlowStart()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, InRecovery()).Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, PopulateConnectionStats(_)).Times(AnyNumber());
  connection_.SetSendAlgorithm(send_algorithm_);

  // Receive probing packet with a newer peer address shouldn't override the
  // on-going path validation.
  const QuicSocketAddress kNewerPeerAddress(QuicIpAddress::Loopback4(),
                                            /*port=*/34567);
  EXPECT_CALL(*send_algorithm_, OnPacketSent(_, _, _, _, _))
      .WillOnce(Invoke([&]() {
        EXPECT_EQ(kNewerPeerAddress, writer_->last_write_peer_address());
        EXPECT_FALSE(writer_->path_response_frames().empty());
        EXPECT_TRUE(writer_->path_challenge_frames().empty());
      }));
  QuicPathFrameBuffer path_challenge_payload{0, 1, 2, 3, 4, 5, 6, 7};
  QuicFrames frames1;
  frames1.push_back(
      QuicFrame(new QuicPathChallengeFrame(0, path_challenge_payload)));
  ProcessFramesPacketWithAddresses(frames1, kSelfAddress, kNewerPeerAddress,
                                   ENCRYPTION_FORWARD_SECURE);
  EXPECT_EQ(kNewPeerAddress, connection_.effective_peer_address());
  EXPECT_EQ(kNewPeerAddress, connection_.peer_address());
  EXPECT_TRUE(QuicConnectionPeer::IsAlternativePathValidated(&connection_));
  EXPECT_TRUE(connection_.HasPendingPathValidation());
}

}  // namespace
}  // namespace test
}  // namespace quic
