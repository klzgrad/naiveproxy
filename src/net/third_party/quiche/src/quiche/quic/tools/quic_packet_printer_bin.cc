// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off

// Dumps out the decryptable contents of a QUIC packet in a human-readable way.
// If the packet is null encrypted, this will dump full packet contents.
// Otherwise it will dump the header, and fail with an error that the
// packet is undecryptable.
//
// Usage: quic_packet_printer server|client <hex dump of packet>
//
// Example input:
// quic_packet_printer server 0c6b810308320f24c004a939a38a2e3fd6ca589917f200400201b80b0100501c0700060003023d0000001c00556e656e637279707465642073747265616d2064617461207365656e
//
// Example output:
// OnPacket
// OnUnauthenticatedPublicHeader
// OnUnauthenticatedHeader: { connection_id: 13845207862000976235, connection_id_length:8, packet_number_length:1, multipath_flag: 0, reset_flag: 0, version_flag: 0, path_id: , packet_number: 4 }
// OnDecryptedPacket
// OnPacketHeader
// OnAckFrame:  largest_observed: 1 ack_delay_time: 3000 missing_packets: [  ] is_truncated: 0 received_packets: [ 1 at 466016  ]
// OnStopWaitingFrame
// OnConnectionCloseFrame: error_code { 61 } error_details { Unencrypted stream data seen }

// clang-format on

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/quiche_text_utils.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, quic_version, "",
                                "If set, specify the QUIC version to use.");

namespace quic {

class QuicPacketPrinter : public QuicFramerVisitorInterface {
 public:
  explicit QuicPacketPrinter(QuicFramer* framer) : framer_(framer) {}

  void OnError(QuicFramer* framer) override {
    std::cerr << "OnError: " << QuicErrorCodeToString(framer->error())
              << " detail: " << framer->detailed_error() << "\n";
  }
  bool OnProtocolVersionMismatch(ParsedQuicVersion received_version) override {
    framer_->set_version(received_version);
    std::cerr << "OnProtocolVersionMismatch: "
              << ParsedQuicVersionToString(received_version) << "\n";
    return true;
  }
  void OnPacket() override { std::cerr << "OnPacket\n"; }
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& /*packet*/) override {
    std::cerr << "OnVersionNegotiationPacket\n";
  }
  void OnRetryPacket(QuicConnectionId /*original_connection_id*/,
                     QuicConnectionId /*new_connection_id*/,
                     absl::string_view /*retry_token*/,
                     absl::string_view /*retry_integrity_tag*/,
                     absl::string_view /*retry_without_tag*/) override {
    std::cerr << "OnRetryPacket\n";
  }
  bool OnUnauthenticatedPublicHeader(
      const QuicPacketHeader& /*header*/) override {
    std::cerr << "OnUnauthenticatedPublicHeader\n";
    return true;
  }
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override {
    std::cerr << "OnUnauthenticatedHeader: " << header;
    return true;
  }
  void OnDecryptedPacket(size_t /*length*/, EncryptionLevel level) override {
    // This only currently supports "decrypting" null encrypted packets.
    QUICHE_DCHECK_EQ(ENCRYPTION_INITIAL, level);
    std::cerr << "OnDecryptedPacket\n";
  }
  bool OnPacketHeader(const QuicPacketHeader& /*header*/) override {
    std::cerr << "OnPacketHeader\n";
    return true;
  }
  void OnCoalescedPacket(const QuicEncryptedPacket& /*packet*/) override {
    std::cerr << "OnCoalescedPacket\n";
  }
  void OnUndecryptablePacket(const QuicEncryptedPacket& /*packet*/,
                             EncryptionLevel /*decryption_level*/,
                             bool /*has_decryption_key*/) override {
    std::cerr << "OnUndecryptablePacket\n";
  }
  bool OnStreamFrame(const QuicStreamFrame& frame) override {
    std::cerr << "OnStreamFrame: " << frame;
    std::cerr << "         data: { "
              << absl::BytesToHexString(
                     absl::string_view(frame.data_buffer, frame.data_length))
              << " }\n";
    return true;
  }
  bool OnCryptoFrame(const QuicCryptoFrame& frame) override {
    std::cerr << "OnCryptoFrame: " << frame;
    std::cerr << "         data: { "
              << absl::BytesToHexString(
                     absl::string_view(frame.data_buffer, frame.data_length))
              << " }\n";
    return true;
  }
  bool OnAckFrameStart(QuicPacketNumber largest_acked,
                       QuicTime::Delta /*ack_delay_time*/) override {
    std::cerr << "OnAckFrameStart, largest_acked: " << largest_acked;
    return true;
  }
  bool OnAckRange(QuicPacketNumber start, QuicPacketNumber end) override {
    std::cerr << "OnAckRange: [" << start << ", " << end << ")";
    return true;
  }
  bool OnAckTimestamp(QuicPacketNumber packet_number,
                      QuicTime timestamp) override {
    std::cerr << "OnAckTimestamp: [" << packet_number << ", "
              << timestamp.ToDebuggingValue() << ")";
    return true;
  }
  bool OnAckFrameEnd(QuicPacketNumber start,
                     const std::optional<QuicEcnCounts>& ecn_counts) override {
    std::cerr << "OnAckFrameEnd, start: " << start;
    if (ecn_counts.has_value()) {
      std::cerr << "  ECN counts: " << ecn_counts->ToString();
    }
    return true;
  }
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override {
    std::cerr << "OnStopWaitingFrame: " << frame;
    return true;
  }
  bool OnPaddingFrame(const QuicPaddingFrame& frame) override {
    std::cerr << "OnPaddingFrame: " << frame;
    return true;
  }
  bool OnPingFrame(const QuicPingFrame& frame) override {
    std::cerr << "OnPingFrame: " << frame;
    return true;
  }
  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override {
    std::cerr << "OnRstStreamFrame: " << frame;
    return true;
  }
  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override {
    // The frame printout will indicate whether it's a Google QUIC
    // CONNECTION_CLOSE, IETF QUIC CONNECTION_CLOSE/Transport, or IETF QUIC
    // CONNECTION_CLOSE/Application frame.
    std::cerr << "OnConnectionCloseFrame: " << frame;
    return true;
  }
  bool OnNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame) override {
    std::cerr << "OnNewConnectionIdFrame: " << frame;
    return true;
  }
  bool OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame) override {
    std::cerr << "OnRetireConnectionIdFrame: " << frame;
    return true;
  }
  bool OnNewTokenFrame(const QuicNewTokenFrame& frame) override {
    std::cerr << "OnNewTokenFrame: " << frame;
    return true;
  }
  bool OnStopSendingFrame(const QuicStopSendingFrame& frame) override {
    std::cerr << "OnStopSendingFrame: " << frame;
    return true;
  }
  bool OnPathChallengeFrame(const QuicPathChallengeFrame& frame) override {
    std::cerr << "OnPathChallengeFrame: " << frame;
    return true;
  }
  bool OnPathResponseFrame(const QuicPathResponseFrame& frame) override {
    std::cerr << "OnPathResponseFrame: " << frame;
    return true;
  }
  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override {
    std::cerr << "OnGoAwayFrame: " << frame;
    return true;
  }
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) override {
    std::cerr << "OnMaxStreamsFrame: " << frame;
    return true;
  }
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override {
    std::cerr << "OnStreamsBlockedFrame: " << frame;
    return true;
  }
  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override {
    std::cerr << "OnWindowUpdateFrame: " << frame;
    return true;
  }
  bool OnBlockedFrame(const QuicBlockedFrame& frame) override {
    std::cerr << "OnBlockedFrame: " << frame;
    return true;
  }
  bool OnMessageFrame(const QuicMessageFrame& frame) override {
    std::cerr << "OnMessageFrame: " << frame;
    return true;
  }
  bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) override {
    std::cerr << "OnHandshakeDoneFrame: " << frame;
    return true;
  }
  bool OnAckFrequencyFrame(const QuicAckFrequencyFrame& frame) override {
    std::cerr << "OnAckFrequencyFrame: " << frame;
    return true;
  }
  bool OnResetStreamAtFrame(const QuicResetStreamAtFrame& frame) override {
    std::cerr << "OnResetStreamAtFrame: " << frame;
    return true;
  }
  void OnPacketComplete() override { std::cerr << "OnPacketComplete\n"; }
  bool IsValidStatelessResetToken(
      const StatelessResetToken& /*token*/) const override {
    std::cerr << "IsValidStatelessResetToken\n";
    return false;
  }
  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& /*packet*/) override {
    std::cerr << "OnAuthenticatedIetfStatelessResetPacket\n";
  }
  void OnKeyUpdate(KeyUpdateReason reason) override {
    std::cerr << "OnKeyUpdate: " << reason << "\n";
  }
  void OnDecryptedFirstPacketInKeyPhase() override {
    std::cerr << "OnDecryptedFirstPacketInKeyPhase\n";
  }
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override {
    std::cerr << "AdvanceKeysAndCreateCurrentOneRttDecrypter\n";
    return nullptr;
  }
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override {
    std::cerr << "CreateCurrentOneRttEncrypter\n";
    return nullptr;
  }

 private:
  QuicFramer* framer_;  // Unowned.
};

}  // namespace quic

int main(int argc, char* argv[]) {
  const char* usage = "Usage: quic_packet_printer client|server <hex>";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  if (args.size() < 2) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  std::string perspective_string = args[0];
  quic::Perspective perspective;
  if (perspective_string == "client") {
    perspective = quic::Perspective::IS_CLIENT;
  } else if (perspective_string == "server") {
    perspective = quic::Perspective::IS_SERVER;
  } else {
    std::cerr << "Invalid perspective" << std::endl;
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }
  std::string hex;
  if (!absl::HexStringToBytes(args[1], &hex)) {
    std::cerr << "Invalid hex string" << std::endl;
    return 1;
  }
  quic::ParsedQuicVersionVector versions = quic::AllSupportedVersions();
  // Fake a time since we're not actually generating acks.
  quic::QuicTime start(quic::QuicTime::Zero());
  quic::QuicFramer framer(versions, start, perspective,
                          quic::kQuicDefaultConnectionIdLength);
  const quic::ParsedQuicVersion& version = quic::ParseQuicVersionString(
      quiche::GetQuicheCommandLineFlag(FLAGS_quic_version));
  if (version != quic::ParsedQuicVersion::Unsupported()) {
    framer.set_version(version);
  }
  quic::QuicPacketPrinter visitor(&framer);
  framer.set_visitor(&visitor);
  quic::QuicEncryptedPacket encrypted(hex.c_str(), hex.length());
  return framer.ProcessPacket(encrypted);
}
