// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_

#include <string>

#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/tls_server_connection.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/tls_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// An implementation of QuicCryptoServerStream::HandshakerDelegate which uses
// TLS 1.3 for the crypto handshake protocol.
class QUIC_EXPORT_PRIVATE TlsServerHandshaker
    : public TlsHandshaker,
      public TlsServerConnection::Delegate,
      public QuicCryptoServerStream::HandshakerDelegate {
 public:
  TlsServerHandshaker(QuicCryptoStream* stream,
                      QuicSession* session,
                      SSL_CTX* ssl_ctx,
                      ProofSource* proof_source);
  TlsServerHandshaker(const TlsServerHandshaker&) = delete;
  TlsServerHandshaker& operator=(const TlsServerHandshaker&) = delete;

  ~TlsServerHandshaker() override;

  // Creates and configures an SSL_CTX to be used with a TlsServerHandshaker.
  // The caller is responsible for ownership of the newly created struct.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx();

  // From QuicCryptoServerStream::HandshakerDelegate
  void CancelOutstandingCallbacks() override;
  bool GetBase64SHA256ClientChannelID(std::string* output) const override;
  void SendServerConfigUpdate(
      const CachedNetworkParameters* cached_network_params) override;
  uint8_t NumHandshakeMessages() const override;
  uint8_t NumHandshakeMessagesWithServerNonces() const override;
  int NumServerConfigUpdateMessagesSent() const override;
  const CachedNetworkParameters* PreviousCachedNetworkParams() const override;
  bool ZeroRttAttempted() const override;
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) override;
  bool ShouldSendExpectCTHeader() const override;

  // From QuicCryptoServerStream::HandshakerDelegate and TlsHandshaker
  bool encryption_established() const override;
  bool handshake_confirmed() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;

 protected:
  const TlsConnection* tls_connection() const override {
    return &tls_connection_;
  }

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
  ssl_private_key_result_t PrivateKeySign(uint8_t* out,
                                          size_t* out_len,
                                          size_t max_out,
                                          uint16_t sig_alg,
                                          QuicStringPiece in) override;
  ssl_private_key_result_t PrivateKeyComplete(uint8_t* out,
                                              size_t* out_len,
                                              size_t max_out) override;
  TlsConnection::Delegate* ConnectionDelegate() override { return this; }

 private:
  class SignatureCallback : public ProofSource::SignatureCallback {
   public:
    explicit SignatureCallback(TlsServerHandshaker* handshaker);
    void Run(bool ok, std::string signature) override;

    // If called, Cancel causes the pending callback to be a no-op.
    void Cancel();

   private:
    TlsServerHandshaker* handshaker_;
  };

  enum State {
    STATE_LISTENING,
    STATE_SIGNATURE_PENDING,
    STATE_SIGNATURE_COMPLETE,
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

  std::string hostname_;
  std::string cert_verify_sig_;

  bool encryption_established_ = false;
  bool handshake_confirmed_ = false;
  bool valid_alpn_received_ = false;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
  TlsServerConnection tls_connection_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_
