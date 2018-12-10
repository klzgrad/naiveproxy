// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_
#define NET_THIRD_PARTY_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_

#include "net/third_party/quic/core/crypto/quic_tls_adapter.h"
#include "net/third_party/quic/core/proto/cached_network_parameters.pb.h"
#include "net/third_party/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quic/core/quic_crypto_stream.h"
#include "net/third_party/quic/core/tls_handshaker.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace quic {

// An implementation of QuicCryptoServerStream::HandshakerDelegate which uses
// TLS 1.3 for the crypto handshake protocol.
class QUIC_EXPORT_PRIVATE TlsServerHandshaker
    : public QuicCryptoServerStream::HandshakerDelegate,
      public TlsHandshaker {
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
  bool GetBase64SHA256ClientChannelID(QuicString* output) const override;
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
  QuicLongHeaderType GetLongHeaderType(QuicStreamOffset offset) const override;
  bool encryption_established() const override;
  bool handshake_confirmed() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;

  // Calls SelectCertificate after looking up the TlsServerHandshaker from
  // |ssl|.
  static int SelectCertificateCallback(SSL* ssl, int* out_alert, void* arg);

 private:
  class SignatureCallback : public ProofSource::SignatureCallback {
   public:
    explicit SignatureCallback(TlsServerHandshaker* handshaker);
    void Run(bool ok, QuicString signature) override;

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

  // |kPrivateKeyMethod| is a vtable pointing to PrivateKeySign and
  // PrivateKeyComplete used by the TLS stack to compute the signature for the
  // CertificateVerify message (using the server's private key).
  static const SSL_PRIVATE_KEY_METHOD kPrivateKeyMethod;

  // Called when a new message is received on the crypto stream and is available
  // for the TLS stack to read.
  void AdvanceHandshake() override;

  // Called when the TLS handshake is complete.
  void FinishHandshake();

  void CloseConnection(const QuicString& reason_phrase);

  bool SetTransportParameters();
  bool ProcessTransportParameters(QuicString* error_details);

  // Calls the instance method PrivateKeySign after looking up the
  // TlsServerHandshaker from |ssl|.
  static ssl_private_key_result_t PrivateKeySign(SSL* ssl,
                                                 uint8_t* out,
                                                 size_t* out_len,
                                                 size_t max_out,
                                                 uint16_t sig_alg,
                                                 const uint8_t* in,
                                                 size_t in_len);

  // Signs |in| using the signature algorithm specified by |sig_alg| (an
  // SSL_SIGN_* value). If the signing operation cannot be completed
  // synchronously, ssl_private_key_retry is returned. If there is an error
  // signing, or if the signature is longer than |max_out|, then
  // ssl_private_key_failure is returned. Otherwise, ssl_private_key_success is
  // returned with the signature put in |*out| and the length in |*out_len|.
  ssl_private_key_result_t PrivateKeySign(uint8_t* out,
                                          size_t* out_len,
                                          size_t max_out,
                                          uint16_t sig_alg,
                                          QuicStringPiece in);

  // Calls the instance method PrivateKeyComplete after looking up the
  // TlsServerHandshaker from |ssl|.
  static ssl_private_key_result_t PrivateKeyComplete(SSL* ssl,
                                                     uint8_t* out,
                                                     size_t* out_len,
                                                     size_t max_out);

  // When PrivateKeySign returns ssl_private_key_retry, PrivateKeyComplete will
  // be called after the async sign operation has completed. PrivateKeyComplete
  // puts the resulting signature in |*out| and length in |*out_len|. If the
  // length is greater than |max_out| or if there was an error in signing, then
  // ssl_private_key_failure is returned. Otherwise, ssl_private_key_success is
  // returned.
  ssl_private_key_result_t PrivateKeyComplete(uint8_t* out,
                                              size_t* out_len,
                                              size_t max_out);

  // Configures the certificate to use on |ssl_| based on the SNI sent by the
  // client. Returns an SSL_TLSEXT_ERR_* value (see
  // https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#SSL_CTX_set_tlsext_servername_callback).
  //
  // If SelectCertificate returns SSL_TLSEXT_ERR_ALERT_FATAL, then it puts in
  // |*out_alert| the TLS alert value that the server will send.
  int SelectCertificate(int* out_alert);

  static TlsServerHandshaker* HandshakerFromSsl(SSL* ssl);

  State state_ = STATE_LISTENING;

  ProofSource* proof_source_;
  SignatureCallback* signature_callback_ = nullptr;

  QuicString hostname_;
  QuicString cert_verify_sig_;

  bool encryption_established_ = false;
  bool handshake_confirmed_ = false;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_TLS_SERVER_HANDSHAKER_H_
