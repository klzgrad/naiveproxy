// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/chlo_extractor.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_framer.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/crypto_handshake_message.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"
#include "quiche/quic/core/frames/quic_reset_stream_at_frame.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"

namespace quic {

namespace {

class ChloFramerVisitor : public QuicFramerVisitorInterface,
                          public CryptoFramerVisitorInterface {
 public:
  ChloFramerVisitor(QuicFramer* framer,
                    const QuicTagVector& create_session_tag_indicators,
                    ChloExtractor::Delegate* delegate);

  ~ChloFramerVisitor() override = default;

  // QuicFramerVisitorInterface implementation
  void OnError(QuicFramer* /*framer*/) override {}
  bool OnProtocolVersionMismatch(ParsedQuicVersion version) override;
  void OnPacket() override {}
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& /*packet*/) override {}
  void OnRetryPacket(QuicConnectionId /*original_connection_id*/,
                     QuicConnectionId /*new_connection_id*/,
                     absl::string_view /*retry_token*/,
                     absl::string_view /*retry_integrity_tag*/,
                     absl::string_view /*retry_without_tag*/) override {}
  bool OnUnauthenticatedPublicHeader(const QuicPacketHeader& header) override;
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override;
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
  bool OnAckFrameEnd(QuicPacketNumber start,
                     const std::optional<QuicEcnCounts>& ecn_counts) override;
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override;
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
  bool OnPaddingFrame(const QuicPaddingFrame& frame) override;
  bool OnMessageFrame(const QuicMessageFrame& frame) override;
  bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) override;
  bool OnAckFrequencyFrame(const QuicAckFrequencyFrame& farme) override;
  bool OnResetStreamAtFrame(const QuicResetStreamAtFrame& frame) override;
  void OnPacketComplete() override {}
  bool IsValidStatelessResetToken(
      const StatelessResetToken& token) const override;
  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& /*packet*/) override {}
  void OnKeyUpdate(KeyUpdateReason /*reason*/) override;
  void OnDecryptedFirstPacketInKeyPhase() override;
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override;
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override;

  // CryptoFramerVisitorInterface implementation.
  void OnError(CryptoFramer* framer) override;
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

  // Shared implementation between OnStreamFrame and OnCryptoFrame.
  bool OnHandshakeData(absl::string_view data);

  bool found_chlo() { return found_chlo_; }
  bool chlo_contains_tags() { return chlo_contains_tags_; }

 private:
  QuicFramer* framer_;
  const QuicTagVector& create_session_tag_indicators_;
  ChloExtractor::Delegate* delegate_;
  bool found_chlo_;
  bool chlo_contains_tags_;
  QuicConnectionId connection_id_;
};

ChloFramerVisitor::ChloFramerVisitor(
    QuicFramer* framer, const QuicTagVector& create_session_tag_indicators,
    ChloExtractor::Delegate* delegate)
    : framer_(framer),
      create_session_tag_indicators_(create_session_tag_indicators),
      delegate_(delegate),
      found_chlo_(false),
      chlo_contains_tags_(false),
      connection_id_(EmptyQuicConnectionId()) {}

bool ChloFramerVisitor::OnProtocolVersionMismatch(ParsedQuicVersion version) {
  if (!framer_->IsSupportedVersion(version)) {
    return false;
  }
  framer_->set_version(version);
  return true;
}

bool ChloFramerVisitor::OnUnauthenticatedPublicHeader(
    const QuicPacketHeader& header) {
  connection_id_ = header.destination_connection_id;
  // QuicFramer creates a NullEncrypter and NullDecrypter at level
  // ENCRYPTION_INITIAL. While those are the correct ones to use with some
  // versions of QUIC, others use the IETF-style initial crypters, so those need
  // to be created and installed.
  framer_->SetInitialObfuscators(header.destination_connection_id);
  return true;
}
bool ChloFramerVisitor::OnUnauthenticatedHeader(
    const QuicPacketHeader& /*header*/) {
  return true;
}
bool ChloFramerVisitor::OnPacketHeader(const QuicPacketHeader& /*header*/) {
  return true;
}

void ChloFramerVisitor::OnCoalescedPacket(
    const QuicEncryptedPacket& /*packet*/) {}

void ChloFramerVisitor::OnUndecryptablePacket(
    const QuicEncryptedPacket& /*packet*/, EncryptionLevel /*decryption_level*/,
    bool /*has_decryption_key*/) {}

bool ChloFramerVisitor::OnStreamFrame(const QuicStreamFrame& frame) {
  if (QuicVersionUsesCryptoFrames(framer_->transport_version())) {
    // CHLO will be sent in CRYPTO frames in v47 and above.
    return false;
  }
  absl::string_view data(frame.data_buffer, frame.data_length);
  if (QuicUtils::IsCryptoStreamId(framer_->transport_version(),
                                  frame.stream_id) &&
      frame.offset == 0 && absl::StartsWith(data, "CHLO")) {
    return OnHandshakeData(data);
  }
  return true;
}

bool ChloFramerVisitor::OnCryptoFrame(const QuicCryptoFrame& frame) {
  if (!QuicVersionUsesCryptoFrames(framer_->transport_version())) {
    // CHLO will be in stream frames before v47.
    return false;
  }
  absl::string_view data(frame.data_buffer, frame.data_length);
  if (frame.offset == 0 && absl::StartsWith(data, "CHLO")) {
    return OnHandshakeData(data);
  }
  return true;
}

bool ChloFramerVisitor::OnHandshakeData(absl::string_view data) {
  CryptoFramer crypto_framer;
  crypto_framer.set_visitor(this);
  if (!crypto_framer.ProcessInput(data)) {
    return false;
  }
  // Interrogate the crypto framer and see if there are any
  // intersecting tags between what we saw in the maybe-CHLO and the
  // indicator set.
  for (const QuicTag tag : create_session_tag_indicators_) {
    if (crypto_framer.HasTag(tag)) {
      chlo_contains_tags_ = true;
    }
  }
  if (chlo_contains_tags_ && delegate_) {
    // Unfortunately, because this is a partial CHLO,
    // OnHandshakeMessage was never called, so the ALPN was never
    // extracted. Fake it up a bit and send it to the delegate so that
    // the correct dispatch can happen.
    crypto_framer.ForceHandshake();
  }

  return true;
}

bool ChloFramerVisitor::OnAckFrameStart(QuicPacketNumber /*largest_acked*/,
                                        QuicTime::Delta /*ack_delay_time*/) {
  return true;
}

bool ChloFramerVisitor::OnResetStreamAtFrame(
    const QuicResetStreamAtFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnAckRange(QuicPacketNumber /*start*/,
                                   QuicPacketNumber /*end*/) {
  return true;
}

bool ChloFramerVisitor::OnAckTimestamp(QuicPacketNumber /*packet_number*/,
                                       QuicTime /*timestamp*/) {
  return true;
}

bool ChloFramerVisitor::OnAckFrameEnd(
    QuicPacketNumber /*start*/,
    const std::optional<QuicEcnCounts>& /*ecn_counts*/) {
  return true;
}

bool ChloFramerVisitor::OnStopWaitingFrame(
    const QuicStopWaitingFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnPingFrame(const QuicPingFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnRstStreamFrame(const QuicRstStreamFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnConnectionCloseFrame(
    const QuicConnectionCloseFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnStopSendingFrame(
    const QuicStopSendingFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnPathChallengeFrame(
    const QuicPathChallengeFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnPathResponseFrame(
    const QuicPathResponseFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnGoAwayFrame(const QuicGoAwayFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnWindowUpdateFrame(
    const QuicWindowUpdateFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnBlockedFrame(const QuicBlockedFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnNewTokenFrame(const QuicNewTokenFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnPaddingFrame(const QuicPaddingFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnMessageFrame(const QuicMessageFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnHandshakeDoneFrame(
    const QuicHandshakeDoneFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnAckFrequencyFrame(
    const QuicAckFrequencyFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::IsValidStatelessResetToken(
    const StatelessResetToken& /*token*/) const {
  return false;
}

bool ChloFramerVisitor::OnMaxStreamsFrame(
    const QuicMaxStreamsFrame& /*frame*/) {
  return true;
}

bool ChloFramerVisitor::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& /*frame*/) {
  return true;
}

void ChloFramerVisitor::OnKeyUpdate(KeyUpdateReason /*reason*/) {}

void ChloFramerVisitor::OnDecryptedFirstPacketInKeyPhase() {}

std::unique_ptr<QuicDecrypter>
ChloFramerVisitor::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  return nullptr;
}

std::unique_ptr<QuicEncrypter>
ChloFramerVisitor::CreateCurrentOneRttEncrypter() {
  return nullptr;
}

void ChloFramerVisitor::OnError(CryptoFramer* /*framer*/) {}

void ChloFramerVisitor::OnHandshakeMessage(
    const CryptoHandshakeMessage& message) {
  if (delegate_ != nullptr) {
    delegate_->OnChlo(framer_->transport_version(), connection_id_, message);
  }
  found_chlo_ = true;
}

}  // namespace

// static
bool ChloExtractor::Extract(const QuicEncryptedPacket& packet,
                            ParsedQuicVersion version,
                            const QuicTagVector& create_session_tag_indicators,
                            Delegate* delegate, uint8_t connection_id_length) {
  QUIC_DVLOG(1) << "Extracting CHLO using version " << version;
  QuicFramer framer({version}, QuicTime::Zero(), Perspective::IS_SERVER,
                    connection_id_length);
  ChloFramerVisitor visitor(&framer, create_session_tag_indicators, delegate);
  framer.set_visitor(&visitor);
  if (!framer.ProcessPacket(packet)) {
    return false;
  }
  return visitor.found_chlo() || visitor.chlo_contains_tags();
}

}  // namespace quic
