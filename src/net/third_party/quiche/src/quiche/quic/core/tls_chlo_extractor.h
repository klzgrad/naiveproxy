// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_CHLO_EXTRACTOR_H_
#define QUICHE_QUIC_CORE_TLS_CHLO_EXTRACTOR_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"
#include "quiche/quic/core/frames/quic_reset_stream_at_frame.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Utility class that allows extracting information from a QUIC-TLS Client
// Hello. This class creates a QuicFramer to parse the packet, and implements
// QuicFramerVisitorInterface to access the frames parsed by the QuicFramer. It
// then uses a QuicStreamSequencer to reassemble the contents of the crypto
// stream, and implements QuicStreamSequencer::StreamInterface to access the
// reassembled data.
class QUICHE_EXPORT TlsChloExtractor
    : public QuicFramerVisitorInterface,
      public QuicStreamSequencer::StreamInterface {
 public:
  TlsChloExtractor();
  TlsChloExtractor(const TlsChloExtractor&) = delete;
  TlsChloExtractor(TlsChloExtractor&&);
  TlsChloExtractor& operator=(const TlsChloExtractor&) = delete;
  TlsChloExtractor& operator=(TlsChloExtractor&&);

  enum class State : uint8_t {
    kInitial = 0,
    kParsedFullSinglePacketChlo = 1,
    kParsedFullMultiPacketChlo = 2,
    kParsedPartialChloFragment = 3,
    kUnrecoverableFailure = 4,
  };

  State state() const { return state_; }
  std::vector<std::string> alpns() const { return alpns_; }
  std::string server_name() const { return server_name_; }
  bool resumption_attempted() const { return resumption_attempted_; }
  bool early_data_attempted() const { return early_data_attempted_; }
  const std::vector<uint16_t>& supported_groups() const {
    return supported_groups_;
  }
  absl::Span<const uint8_t> client_hello_bytes() const {
    return client_hello_bytes_;
  }

  // Converts |state| to a human-readable string suitable for logging.
  static std::string StateToString(State state);

  // Ingests |packet| and attempts to parse out the CHLO.
  void IngestPacket(const ParsedQuicVersion& version,
                    const QuicReceivedPacket& packet);

  // Returns whether the ingested packets have allowed parsing a complete CHLO.
  bool HasParsedFullChlo() const {
    return state_ == State::kParsedFullSinglePacketChlo ||
           state_ == State::kParsedFullMultiPacketChlo;
  }

  // Returns the TLS alert that caused the unrecoverable error, if any.
  std::optional<uint8_t> tls_alert() const {
    QUICHE_DCHECK(!tls_alert_.has_value() ||
                  state_ == State::kUnrecoverableFailure);
    return tls_alert_;
  }

  // Methods from QuicFramerVisitorInterface.
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
  bool OnUnauthenticatedHeader(const QuicPacketHeader& /*header*/) override {
    return true;
  }
  void OnDecryptedPacket(size_t /*packet_length*/,
                         EncryptionLevel /*level*/) override {}
  bool OnPacketHeader(const QuicPacketHeader& /*header*/) override {
    return true;
  }
  void OnCoalescedPacket(const QuicEncryptedPacket& /*packet*/) override {}
  void OnUndecryptablePacket(const QuicEncryptedPacket& /*packet*/,
                             EncryptionLevel /*decryption_level*/,
                             bool /*has_decryption_key*/) override {}
  bool OnStreamFrame(const QuicStreamFrame& /*frame*/) override { return true; }
  bool OnCryptoFrame(const QuicCryptoFrame& frame) override;
  bool OnAckFrameStart(QuicPacketNumber /*largest_acked*/,
                       QuicTime::Delta /*ack_delay_time*/) override {
    return true;
  }
  bool OnAckRange(QuicPacketNumber /*start*/,
                  QuicPacketNumber /*end*/) override {
    return true;
  }
  bool OnAckTimestamp(QuicPacketNumber /*packet_number*/,
                      QuicTime /*timestamp*/) override {
    return true;
  }
  bool OnAckFrameEnd(
      QuicPacketNumber /*start*/,
      const std::optional<QuicEcnCounts>& /*ecn_counts*/) override {
    return true;
  }
  bool OnStopWaitingFrame(const QuicStopWaitingFrame& /*frame*/) override {
    return true;
  }
  bool OnPingFrame(const QuicPingFrame& /*frame*/) override { return true; }
  bool OnRstStreamFrame(const QuicRstStreamFrame& /*frame*/) override {
    return true;
  }
  bool OnConnectionCloseFrame(
      const QuicConnectionCloseFrame& /*frame*/) override {
    return true;
  }
  bool OnNewConnectionIdFrame(
      const QuicNewConnectionIdFrame& /*frame*/) override {
    return true;
  }
  bool OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& /*frame*/) override {
    return true;
  }
  bool OnNewTokenFrame(const QuicNewTokenFrame& /*frame*/) override {
    return true;
  }
  bool OnStopSendingFrame(const QuicStopSendingFrame& /*frame*/) override {
    return true;
  }
  bool OnPathChallengeFrame(const QuicPathChallengeFrame& /*frame*/) override {
    return true;
  }
  bool OnPathResponseFrame(const QuicPathResponseFrame& /*frame*/) override {
    return true;
  }
  bool OnGoAwayFrame(const QuicGoAwayFrame& /*frame*/) override { return true; }
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& /*frame*/) override {
    return true;
  }
  bool OnStreamsBlockedFrame(
      const QuicStreamsBlockedFrame& /*frame*/) override {
    return true;
  }
  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& /*frame*/) override {
    return true;
  }
  bool OnBlockedFrame(const QuicBlockedFrame& /*frame*/) override {
    return true;
  }
  bool OnPaddingFrame(const QuicPaddingFrame& /*frame*/) override {
    return true;
  }
  bool OnMessageFrame(const QuicMessageFrame& /*frame*/) override {
    return true;
  }
  bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& /*frame*/) override {
    return true;
  }
  bool OnAckFrequencyFrame(const QuicAckFrequencyFrame& /*frame*/) override {
    return true;
  }
  bool OnResetStreamAtFrame(const QuicResetStreamAtFrame& /*frame*/) override {
    return true;
  }
  void OnPacketComplete() override {}
  bool IsValidStatelessResetToken(
      const StatelessResetToken& /*token*/) const override {
    return true;
  }
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

  // Methods from QuicStreamSequencer::StreamInterface.
  void OnDataAvailable() override;
  void OnFinRead() override {}
  void AddBytesConsumed(QuicByteCount /*bytes*/) override {}
  void ResetWithError(QuicResetStreamError /*error*/) override {}
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& details) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            QuicIetfTransportErrorCodes ietf_error,
                            const std::string& details) override;
  QuicStreamId id() const override { return 0; }
  ParsedQuicVersion version() const override { return framer_->version(); }

 private:
  // Parses the length of the CHLO message by looking at the first four bytes.
  // Returns whether we have received enough data to parse the full CHLO now.
  bool MaybeAttemptToParseChloLength();
  // Parses the full CHLO message if enough data has been received.
  void AttemptToParseFullChlo();
  // Moves to the failed state and records the error details.
  void HandleUnrecoverableError(const std::string& error_details);
  // Lazily sets up shared SSL handles if needed.
  static std::pair<SSL_CTX*, int> GetSharedSslHandles();
  // Lazily sets up the per-instance SSL handle if needed.
  void SetupSslHandle();
  // Extract the TlsChloExtractor instance from |ssl|.
  static TlsChloExtractor* GetInstanceFromSSL(SSL* ssl);

  // BoringSSL static TLS callbacks.
  static enum ssl_select_cert_result_t SelectCertCallback(
      const SSL_CLIENT_HELLO* client_hello);
  static int SetReadSecretCallback(SSL* ssl, enum ssl_encryption_level_t level,
                                   const SSL_CIPHER* cipher,
                                   const uint8_t* secret, size_t secret_length);
  static int SetWriteSecretCallback(SSL* ssl, enum ssl_encryption_level_t level,
                                    const SSL_CIPHER* cipher,
                                    const uint8_t* secret,
                                    size_t secret_length);
  static int WriteMessageCallback(SSL* ssl, enum ssl_encryption_level_t level,
                                  const uint8_t* data, size_t len);
  static int FlushFlightCallback(SSL* ssl);
  static int SendAlertCallback(SSL* ssl, enum ssl_encryption_level_t level,
                               uint8_t desc);

  // Called by SelectCertCallback.
  void HandleParsedChlo(const SSL_CLIENT_HELLO* client_hello);
  // Called by callbacks that should never be called.
  void HandleUnexpectedCallback(const std::string& callback_name);
  // Called by SendAlertCallback.
  void SendAlert(uint8_t tls_alert_value);

  // Used to parse received packets to extract single frames.
  std::unique_ptr<QuicFramer> framer_;
  // Used to reassemble the crypto stream from received CRYPTO frames.
  QuicStreamSequencer crypto_stream_sequencer_;
  // BoringSSL handle required to parse the CHLO.
  bssl::UniquePtr<SSL> ssl_;
  // State of this TlsChloExtractor.
  State state_;
  // Detail string that can be logged in the presence of unrecoverable errors.
  std::string error_details_;
  // Whether a CRYPTO frame was parsed in this packet.
  bool parsed_crypto_frame_in_this_packet_;
  // Array of NamedGroups parsed from the CHLO's supported_groups extension.
  std::vector<uint16_t> supported_groups_;
  // Array of ALPNs parsed from the CHLO.
  std::vector<std::string> alpns_;
  // SNI parsed from the CHLO.
  std::string server_name_;
  // Whether resumption is attempted from the CHLO, indicated by the
  // 'pre_shared_key' TLS extension.
  bool resumption_attempted_ = false;
  // Whether early data is attempted from the CHLO, indicated by the
  // 'early_data' TLS extension.
  bool early_data_attempted_ = false;
  // If set, contains the TLS alert that caused an unrecoverable error, which is
  // an AlertDescription value defined in go/rfc/8446#appendix-B.2.
  std::optional<uint8_t> tls_alert_;
  // Exact TLS message bytes.
  std::vector<uint8_t> client_hello_bytes_;
};

// Convenience method to facilitate logging TlsChloExtractor::State.
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const TlsChloExtractor::State& state);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_CHLO_EXTRACTOR_H_
