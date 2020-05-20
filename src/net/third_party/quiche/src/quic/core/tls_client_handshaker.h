// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_CLIENT_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_TLS_CLIENT_HANDSHAKER_H_

#include <string>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quiche/src/quic/core/crypto/tls_client_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/tls_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// An implementation of QuicCryptoClientStream::HandshakerInterface which uses
// TLS 1.3 for the crypto handshake protocol.
class QUIC_EXPORT_PRIVATE TlsClientHandshaker
    : public TlsHandshaker,
      public QuicCryptoClientStream::HandshakerInterface,
      public TlsClientConnection::Delegate {
 public:
  TlsClientHandshaker(const QuicServerId& server_id,
                      QuicCryptoStream* stream,
                      QuicSession* session,
                      std::unique_ptr<ProofVerifyContext> verify_context,
                      QuicCryptoClientConfig* crypto_config,
                      QuicCryptoClientStream::ProofHandler* proof_handler);
  TlsClientHandshaker(const TlsClientHandshaker&) = delete;
  TlsClientHandshaker& operator=(const TlsClientHandshaker&) = delete;

  ~TlsClientHandshaker() override;

  // From QuicCryptoClientStream::HandshakerInterface
  bool CryptoConnect() override;
  int num_sent_client_hellos() const override;
  bool IsResumption() const override;
  bool EarlyDataAccepted() const override;
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
  void OnOneRttPacketAcknowledged() override;
  void OnHandshakeDoneReceived() override;
  void SetWriteSecret(EncryptionLevel level,
                      const SSL_CIPHER* cipher,
                      const std::vector<uint8_t>& write_secret) override;

  // Override to drop initial keys if trying to write ENCRYPTION_HANDSHAKE data.
  void WriteMessage(EncryptionLevel level,
                    quiche::QuicheStringPiece data) override;

  void AllowEmptyAlpnForTests() { allow_empty_alpn_for_tests_ = true; }

 protected:
  const TlsConnection* tls_connection() const override {
    return &tls_connection_;
  }

  void AdvanceHandshake() override;
  void CloseConnection(QuicErrorCode error,
                       const std::string& reason_phrase) override;

  // TlsClientConnection::Delegate implementation:
  enum ssl_verify_result_t VerifyCert(uint8_t* out_alert) override;
  TlsConnection::Delegate* ConnectionDelegate() override { return this; }

 private:
  // ProofVerifierCallbackImpl handles the result of an asynchronous certificate
  // verification operation.
  class QUIC_EXPORT_PRIVATE ProofVerifierCallbackImpl
      : public ProofVerifierCallback {
   public:
    explicit ProofVerifierCallbackImpl(TlsClientHandshaker* parent);
    ~ProofVerifierCallbackImpl() override;

    // ProofVerifierCallback interface.
    void Run(bool ok,
             const std::string& error_details,
             std::unique_ptr<ProofVerifyDetails>* details) override;

    // If called, Cancel causes the pending callback to be a no-op.
    void Cancel();

   private:
    TlsClientHandshaker* parent_;
  };

  enum State {
    STATE_IDLE,
    STATE_HANDSHAKE_RUNNING,
    STATE_CERT_VERIFY_PENDING,
    STATE_ENCRYPTION_HANDSHAKE_DATA_SENT,
    STATE_HANDSHAKE_COMPLETE,
    STATE_CONNECTION_CLOSED,
  } state_ = STATE_IDLE;

  bool SetAlpn();
  bool SetTransportParameters();
  bool ProcessTransportParameters(std::string* error_details);
  void FinishHandshake();

  // Called when server completes handshake (i.e., either handshake done is
  // received or 1-RTT packet gets acknowledged).
  void OnHandshakeConfirmed();

  void InsertSession(bssl::UniquePtr<SSL_SESSION> session) override;

  QuicSession* session() { return session_; }
  QuicSession* session_;

  QuicServerId server_id_;

  // Objects used for verifying the server's certificate chain.
  // |proof_verifier_| is owned by the caller of TlsClientHandshaker's
  // constructor.
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

  // ProofVerifierCallback used for async certificate verification. This object
  // is owned by |proof_verifier_|.
  ProofVerifierCallbackImpl* proof_verify_callback_ = nullptr;
  std::unique_ptr<ProofVerifyDetails> verify_details_;
  enum ssl_verify_result_t verify_result_ = ssl_verify_retry;
  std::string cert_verify_error_details_;

  bool encryption_established_ = false;
  bool one_rtt_keys_available_ = false;
  bool handshake_confirmed_ = false;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;

  bool allow_empty_alpn_for_tests_ = false;

  TlsClientConnection tls_connection_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_CLIENT_HANDSHAKER_H_
