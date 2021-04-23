// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_CLIENT_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_TLS_CLIENT_HANDSHAKER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "quic/core/crypto/quic_crypto_client_config.h"
#include "quic/core/crypto/tls_client_connection.h"
#include "quic/core/crypto/transport_parameters.h"
#include "quic/core/quic_crypto_client_stream.h"
#include "quic/core/quic_crypto_stream.h"
#include "quic/core/tls_handshaker.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

// An implementation of QuicCryptoClientStream::HandshakerInterface which uses
// TLS 1.3 for the crypto handshake protocol.
class QUIC_EXPORT_PRIVATE TlsClientHandshaker
    : public TlsHandshaker,
      public QuicCryptoClientStream::HandshakerInterface,
      public TlsClientConnection::Delegate {
 public:
  // |crypto_config| must outlive TlsClientHandshaker.
  TlsClientHandshaker(const QuicServerId& server_id,
                      QuicCryptoStream* stream,
                      QuicSession* session,
                      std::unique_ptr<ProofVerifyContext> verify_context,
                      QuicCryptoClientConfig* crypto_config,
                      QuicCryptoClientStream::ProofHandler* proof_handler,
                      bool has_application_state);
  TlsClientHandshaker(const TlsClientHandshaker&) = delete;
  TlsClientHandshaker& operator=(const TlsClientHandshaker&) = delete;

  ~TlsClientHandshaker() override;

  // From QuicCryptoClientStream::HandshakerInterface
  bool CryptoConnect() override;
  int num_sent_client_hellos() const override;
  bool IsResumption() const override;
  bool EarlyDataAccepted() const override;
  ssl_early_data_reason_t EarlyDataReason() const override;
  bool ReceivedInchoateReject() const override;
  int num_scup_messages_received() const override;
  std::string chlo_hash() const override;

  // From QuicCryptoClientStream::HandshakerInterface and TlsHandshaker
  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  HandshakeState GetHandshakeState() const override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;
  bool KeyUpdateSupportedLocally() const override;
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override;
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override;
  void OnOneRttPacketAcknowledged() override;
  void OnHandshakePacketSent() override;
  void OnConnectionClosed(QuicErrorCode error,
                          ConnectionCloseSource source) override;
  void OnHandshakeDoneReceived() override;
  void OnNewTokenReceived(absl::string_view token) override;
  void SetWriteSecret(EncryptionLevel level,
                      const SSL_CIPHER* cipher,
                      const std::vector<uint8_t>& write_secret) override;

  // Override to drop initial keys if trying to write ENCRYPTION_HANDSHAKE data.
  void WriteMessage(EncryptionLevel level, absl::string_view data) override;

  void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> application_state) override;

  void AllowEmptyAlpnForTests() { allow_empty_alpn_for_tests_ = true; }
  void AllowInvalidSNIForTests() { allow_invalid_sni_for_tests_ = true; }
  SSL* GetSslForTests() { return tls_connection_.ssl(); }
  const SSL* GetSslForTests() const { return tls_connection_.ssl(); }

 protected:
  const TlsConnection* tls_connection() const override {
    return &tls_connection_;
  }

  void FinishHandshake() override;
  void ProcessPostHandshakeMessage() override;
  bool ShouldCloseConnectionOnUnexpectedError(int ssl_error) override;
  QuicAsyncStatus VerifyCertChain(
      const std::vector<std::string>& certs,
      std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      uint8_t* out_alert,
      std::unique_ptr<ProofVerifierCallback> callback) override;
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) override;

  // TlsClientConnection::Delegate implementation:
  TlsConnection::Delegate* ConnectionDelegate() override { return this; }

 private:
  bool SetAlpn();
  bool SetTransportParameters();
  bool ProcessTransportParameters(std::string* error_details);
  void HandleZeroRttReject();

  // Called when server completes handshake (i.e., either handshake done is
  // received or 1-RTT packet gets acknowledged).
  void OnHandshakeConfirmed();

  void InsertSession(bssl::UniquePtr<SSL_SESSION> session) override;

  bool PrepareZeroRttConfig(QuicResumptionState* cached_state);

  QuicSession* session() { return session_; }
  QuicSession* session_;

  QuicServerId server_id_;

  // Objects used for verifying the server's certificate chain.
  // |proof_verifier_| is owned by the caller of TlsHandshaker's constructor.
  ProofVerifier* proof_verifier_;
  std::unique_ptr<ProofVerifyContext> verify_context_;

  // Unowned pointer to the proof handler which has the
  // OnProofVerifyDetailsAvailable callback to use for notifying the result of
  // certificate verification.
  QuicCryptoClientStream::ProofHandler* proof_handler_;

  // Used for session resumption. |session_cache_| is owned by the
  // QuicCryptoClientConfig passed into TlsClientHandshaker's constructor.
  SessionCache* session_cache_;

  std::string user_agent_id_;

  // Pre-shared key used during the handshake.
  std::string pre_shared_key_;

  HandshakeState state_ = HANDSHAKE_START;
  bool encryption_established_ = false;
  bool initial_keys_dropped_ = false;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;

  bool allow_empty_alpn_for_tests_ = false;
  bool allow_invalid_sni_for_tests_ = false;

  const bool has_application_state_;
  // Contains the state for performing a resumption, if one is attempted. This
  // will always be non-null if a 0-RTT resumption is attempted.
  std::unique_ptr<QuicResumptionState> cached_state_;

  QuicCryptoClientConfig* crypto_config_;  // Not owned.

  TlsClientConnection tls_connection_;

  // If |has_application_state_|, stores the tls session tickets before
  // application state is received. The latest one is put in the front.
  bssl::UniquePtr<SSL_SESSION> cached_tls_sessions_[2] = {};

  std::unique_ptr<TransportParameters> received_transport_params_ = nullptr;
  std::unique_ptr<ApplicationState> received_application_state_ = nullptr;

  // Latched value of reloadable flag quic_enable_alps_client.
  const bool enable_alps_ = GetQuicReloadableFlag(quic_enable_alps_client);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_CLIENT_HANDSHAKER_H_
