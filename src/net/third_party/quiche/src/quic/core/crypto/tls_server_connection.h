// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TLS_SERVER_CONNECTION_H_
#define QUICHE_QUIC_CORE_CRYPTO_TLS_SERVER_CONNECTION_H_

#include "net/third_party/quiche/src/quic/core/crypto/tls_connection.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// TlsServerConnection receives calls for client-specific BoringSSL callbacks
// and calls its Delegate for the implementation of those callbacks.
class QUIC_EXPORT_PRIVATE TlsServerConnection : public TlsConnection {
 public:
  // A TlsServerConnection::Delegate implement the server-specific methods that
  // are set as callbacks for an SSL object.
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

   protected:
    // Configures the certificate to use on |ssl_| based on the SNI sent by the
    // client. Returns an SSL_TLSEXT_ERR_* value (see
    // https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#SSL_CTX_set_tlsext_servername_callback).
    //
    // If SelectCertificate returns SSL_TLSEXT_ERR_ALERT_FATAL, then it puts in
    // |*out_alert| the TLS alert value that the server will send.
    virtual int SelectCertificate(int* out_alert) = 0;

    // Selects which ALPN to use based on the list sent by the client.
    virtual int SelectAlpn(const uint8_t** out,
                           uint8_t* out_len,
                           const uint8_t* in,
                           unsigned in_len) = 0;

    // Signs |in| using the signature algorithm specified by |sig_alg| (an
    // SSL_SIGN_* value). If the signing operation cannot be completed
    // synchronously, ssl_private_key_retry is returned. If there is an error
    // signing, or if the signature is longer than |max_out|, then
    // ssl_private_key_failure is returned. Otherwise, ssl_private_key_success
    // is returned with the signature put in |*out| and the length in
    // |*out_len|.
    virtual ssl_private_key_result_t PrivateKeySign(
        uint8_t* out,
        size_t* out_len,
        size_t max_out,
        uint16_t sig_alg,
        quiche::QuicheStringPiece in) = 0;

    // When PrivateKeySign returns ssl_private_key_retry, PrivateKeyComplete
    // will be called after the async sign operation has completed.
    // PrivateKeyComplete puts the resulting signature in |*out| and length in
    // |*out_len|. If the length is greater than |max_out| or if there was an
    // error in signing, then ssl_private_key_failure is returned. Otherwise,
    // ssl_private_key_success is returned.
    virtual ssl_private_key_result_t PrivateKeyComplete(uint8_t* out,
                                                        size_t* out_len,
                                                        size_t max_out) = 0;

    // Provides the delegate for callbacks that are shared between client and
    // server.
    virtual TlsConnection::Delegate* ConnectionDelegate() = 0;

    friend class TlsServerConnection;
  };

  TlsServerConnection(SSL_CTX* ssl_ctx, Delegate* delegate);

  // Creates and configures an SSL_CTX that is appropriate for servers to use.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx();

  void SetCertChain(const std::vector<CRYPTO_BUFFER*>& cert_chain);

 private:
  // Specialization of TlsConnection::ConnectionFromSsl.
  static TlsServerConnection* ConnectionFromSsl(SSL* ssl);

  // These functions are registered as callbacks in BoringSSL and delegate their
  // implementation to the matching methods in Delegate above.
  static int SelectCertificateCallback(SSL* ssl, int* out_alert, void* arg);
  static int SelectAlpnCallback(SSL* ssl,
                                const uint8_t** out,
                                uint8_t* out_len,
                                const uint8_t* in,
                                unsigned in_len,
                                void* arg);

  // |kPrivateKeyMethod| is a vtable pointing to PrivateKeySign and
  // PrivateKeyComplete used by the TLS stack to compute the signature for the
  // CertificateVerify message (using the server's private key).
  static const SSL_PRIVATE_KEY_METHOD kPrivateKeyMethod;

  // The following functions make up the contents of |kPrivateKeyMethod|.
  static ssl_private_key_result_t PrivateKeySign(SSL* ssl,
                                                 uint8_t* out,
                                                 size_t* out_len,
                                                 size_t max_out,
                                                 uint16_t sig_alg,
                                                 const uint8_t* in,
                                                 size_t in_len);
  static ssl_private_key_result_t PrivateKeyComplete(SSL* ssl,
                                                     uint8_t* out,
                                                     size_t* out_len,
                                                     size_t max_out);

  Delegate* delegate_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TLS_SERVER_CONNECTION_H_
