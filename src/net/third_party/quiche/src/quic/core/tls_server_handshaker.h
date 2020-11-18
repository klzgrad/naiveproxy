// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_

#include <string>

#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/tls_server_connection.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream_base.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/tls_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// An implementation of QuicCryptoServerStreamBase which uses
// TLS 1.3 for the crypto handshake protocol.
class QUIC_EXPORT_PRIVATE TlsServerHandshaker
    : public TlsHandshaker,
      public TlsServerConnection::Delegate,
      public QuicCryptoServerStreamBase {
 public:
  TlsServerHandshaker(QuicSession* session,
                      const QuicCryptoServerConfig& crypto_config);
  TlsServerHandshaker(const TlsServerHandshaker&) = delete;
  TlsServerHandshaker& operator=(const TlsServerHandshaker&) = delete;

  ~TlsServerHandshaker() override;

  // From QuicCryptoServerStreamBase
  void CancelOutstandingCallbacks() override;
  bool GetBase64SHA256ClientChannelID(std::string* output) const override;
  void SendServerConfigUpdate(
      const CachedNetworkParameters* cached_network_params) override;
  bool IsZeroRtt() const override;
  bool IsResumption() const override;
  bool ResumptionAttempted() const override;
  int NumServerConfigUpdateMessagesSent() const override;
  const CachedNetworkParameters* PreviousCachedNetworkParams() const override;
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) override;
  void OnPacketDecrypted(EncryptionLevel level) override;
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakePacketSent() override {}
  void OnConnectionClosed(QuicErrorCode error,
                          ConnectionCloseSource source) override;
  void OnHandshakeDoneReceived() override;
  bool ShouldSendExpectCTHeader() const override;
  const ProofSource::Details* ProofSourceDetails() const override;

  // From QuicCryptoServerStreamBase and TlsHandshaker
  ssl_early_data_reason_t EarlyDataReason() const override;
  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  HandshakeState GetHandshakeState() const override;
  void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> state) override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;
  void SetWriteSecret(EncryptionLevel level,
                      const SSL_CIPHER* cipher,
                      const std::vector<uint8_t>& write_secret) override;

 protected:
  // Hook to allow the server to override parts of the QuicConfig based on SNI
  // before we generate transport parameters.
  virtual void OverrideQuicConfigDefaults(QuicConfig* config);

  const TlsConnection* tls_connection() const override {
    return &tls_connection_;
  }

  virtual void ProcessAdditionalTransportParameters(
      const TransportParameters& /*params*/) {}

  // Called when a new message is received on the crypto stream and is available
  // for the TLS stack to read.
  void AdvanceHandshake() override;
  void CloseConnection(QuicErrorCode error,
                       const std::string& reason_phrase) override;

  // TlsServerConnection::Delegate implementation:
  int SelectCertificate(int* out_alert) override;
  int SelectAlpn(const uint8_t** out,
                 uint8_t* out_len,
                 const uint8_t* in,
                 unsigned in_len) override;
  ssl_private_key_result_t PrivateKeySign(
      uint8_t* out,
      size_t* out_len,
      size_t max_out,
      uint16_t sig_alg,
      quiche::QuicheStringPiece in) override;
  ssl_private_key_result_t PrivateKeyComplete(uint8_t* out,
                                              size_t* out_len,
                                              size_t max_out) override;
  size_t SessionTicketMaxOverhead() override;
  int SessionTicketSeal(uint8_t* out,
                        size_t* out_len,
                        size_t max_out_len,
                        quiche::QuicheStringPiece in) override;
  ssl_ticket_aead_result_t SessionTicketOpen(
      uint8_t* out,
      size_t* out_len,
      size_t max_out_len,
      quiche::QuicheStringPiece in) override;
  TlsConnection::Delegate* ConnectionDelegate() override { return this; }

 private:
  class QUIC_EXPORT_PRIVATE SignatureCallback
      : public ProofSource::SignatureCallback {
   public:
    explicit SignatureCallback(TlsServerHandshaker* handshaker);
    void Run(bool ok,
             std::string signature,
             std::unique_ptr<ProofSource::Details> details) override;

    // If called, Cancel causes the pending callback to be a no-op.
    void Cancel();

   private:
    TlsServerHandshaker* handshaker_;
  };

  class QUIC_EXPORT_PRIVATE DecryptCallback
      : public ProofSource::DecryptCallback {
   public:
    explicit DecryptCallback(TlsServerHandshaker* handshaker);
    void Run(std::vector<uint8_t> plaintext) override;

    // If called, Cancel causes the pending callback to be a no-op.
    void Cancel();

   private:
    TlsServerHandshaker* handshaker_;
  };

  enum State {
    STATE_LISTENING,
    STATE_TICKET_DECRYPTION_PENDING,
    STATE_SIGNATURE_PENDING,
    STATE_SIGNATURE_COMPLETE,
    STATE_ENCRYPTION_HANDSHAKE_DATA_PROCESSED,
    STATE_HANDSHAKE_COMPLETE,
    STATE_CONNECTION_CLOSED,
  };

  // Called when the TLS handshake is complete.
  void FinishHandshake();

  void CloseConnection(const std::string& reason_phrase);

  bool SetTransportParameters();
  bool ProcessTransportParameters(std::string* error_details);

  State state_ = STATE_LISTENING;

  ProofSource* proof_source_;
  SignatureCallback* signature_callback_ = nullptr;

  // State to handle potentially asynchronous session ticket decryption.
  // |ticket_decryption_callback_| points to the non-owned callback that was
  // passed to ProofSource::TicketCrypter::Decrypt but hasn't finished running
  // yet.
  DecryptCallback* ticket_decryption_callback_ = nullptr;
  // |decrypted_session_ticket_| contains the decrypted session ticket after the
  // callback has run but before it is passed to BoringSSL.
  std::vector<uint8_t> decrypted_session_ticket_;
  // |ticket_received_| tracks whether we received a resumption ticket from the
  // client. It does not matter whether we were able to decrypt said ticket or
  // if we actually resumed a session with it - the presence of this ticket
  // indicates that the client attempted a resumption.
  bool ticket_received_ = false;

  std::string hostname_;
  std::string cert_verify_sig_;
  std::unique_ptr<ProofSource::Details> proof_source_details_;

  std::unique_ptr<ApplicationState> application_state_;

  // Pre-shared key used during the handshake.
  std::string pre_shared_key_;

  bool encryption_established_ = false;
  bool one_rtt_keys_available_ = false;
  bool valid_alpn_received_ = false;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
  TlsServerConnection tls_connection_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_
