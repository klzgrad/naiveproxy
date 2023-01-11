// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TLS_CLIENT_CONNECTION_H_
#define QUICHE_QUIC_CORE_CRYPTO_TLS_CLIENT_CONNECTION_H_

#include "quiche/quic/core/crypto/tls_connection.h"

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
    // Called when a NewSessionTicket is received from the server.
    virtual void InsertSession(bssl::UniquePtr<SSL_SESSION> session) = 0;

    // Provides the delegate for callbacks that are shared between client and
    // server.
    virtual TlsConnection::Delegate* ConnectionDelegate() = 0;

    friend class TlsClientConnection;
  };

  TlsClientConnection(SSL_CTX* ssl_ctx, Delegate* delegate,
                      QuicSSLConfig ssl_config);

  // Creates and configures an SSL_CTX that is appropriate for clients to use.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx(bool enable_early_data);

  // Set the client cert and private key to be used on this connection, if
  // requested by the server.
  void SetCertChain(const std::vector<CRYPTO_BUFFER*>& cert_chain,
                    EVP_PKEY* privkey);

 private:
  // Registered as the callback for SSL_CTX_sess_set_new_cb, which calls
  // Delegate::InsertSession.
  static int NewSessionCallback(SSL* ssl, SSL_SESSION* session);

  Delegate* delegate_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TLS_CLIENT_CONNECTION_H_
