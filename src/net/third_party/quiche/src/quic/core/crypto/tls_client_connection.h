// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TLS_CLIENT_CONNECTION_H_
#define QUICHE_QUIC_CORE_CRYPTO_TLS_CLIENT_CONNECTION_H_

#include "net/third_party/quiche/src/quic/core/crypto/tls_connection.h"

namespace quic {

// TlsClientConnection receives calls for client-specific BoringSSL callbacks
// and calls its Delegate for the implementation of those callbacks.
class QUIC_EXPORT_PRIVATE TlsClientConnection : public TlsConnection {
 public:
  // A TlsClientConnection::Delegate implements the client-specific methods that
  // are set as callbacks for an SSL object.
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

   protected:
    // Verifies the peer's certificate chain. It may use
    // SSL_get0_peer_certificates to get the cert chain. This method returns
    // ssl_verify_ok if the cert is valid, ssl_verify_invalid if it is invalid,
    // or ssl_verify_retry if verification is happening asynchronously.
    virtual enum ssl_verify_result_t VerifyCert(uint8_t* out_alert) = 0;

    // Called when a NewSessionTicket is received from the server.
    virtual void InsertSession(bssl::UniquePtr<SSL_SESSION> session) = 0;

    // Provides the delegate for callbacks that are shared between client and
    // server.
    virtual TlsConnection::Delegate* ConnectionDelegate() = 0;

    friend class TlsClientConnection;
  };

  TlsClientConnection(SSL_CTX* ssl_ctx, Delegate* delegate);

  // Creates and configures an SSL_CTX that is appropriate for clients to use.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx();

 private:
  // Registered as the callback for SSL_CTX_set_custom_verify. The
  // implementation is delegated to Delegate::VerifyCert.
  static enum ssl_verify_result_t VerifyCallback(SSL* ssl, uint8_t* out_alert);

  // Registered as the callback for SSL_CTX_sess_set_new_cb, which calls
  // Delegate::InsertSession.
  static int NewSessionCallback(SSL* ssl, SSL_SESSION* session);

  Delegate* delegate_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TLS_CLIENT_CONNECTION_H_
