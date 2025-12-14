// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/tls_client_connection.h"

#include <utility>
#include <vector>

namespace quic {

TlsClientConnection::TlsClientConnection(SSL_CTX* ssl_ctx, Delegate* delegate,
                                         QuicSSLConfig ssl_config)
    : TlsConnection(ssl_ctx, delegate->ConnectionDelegate(),
                    std::move(ssl_config)),
      delegate_(delegate) {}

// static
bssl::UniquePtr<SSL_CTX> TlsClientConnection::CreateSslCtx(
    bool enable_early_data) {
  bssl::UniquePtr<SSL_CTX> ssl_ctx = TlsConnection::CreateSslCtx();
  // Configure certificate verification.
  SSL_CTX_set_custom_verify(ssl_ctx.get(), SSL_VERIFY_PEER, &VerifyCallback);
  int reverify_on_resume_enabled = 1;
  SSL_CTX_set_reverify_on_resume(ssl_ctx.get(), reverify_on_resume_enabled);

  // Configure session caching.
  SSL_CTX_set_session_cache_mode(
      ssl_ctx.get(), SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
  SSL_CTX_sess_set_new_cb(ssl_ctx.get(), NewSessionCallback);

  // TODO(wub): Always enable early data on the SSL_CTX, but allow it to be
  // overridden on the SSL object, via QuicSSLConfig.
  SSL_CTX_set_early_data_enabled(ssl_ctx.get(), enable_early_data);
  return ssl_ctx;
}

void TlsClientConnection::SetCertChain(
    const std::vector<CRYPTO_BUFFER*>& cert_chain, EVP_PKEY* privkey) {
  SSL_set_chain_and_key(ssl(), cert_chain.data(), cert_chain.size(), privkey,
                        /*privkey_method=*/nullptr);
}

// static
int TlsClientConnection::NewSessionCallback(SSL* ssl, SSL_SESSION* session) {
  static_cast<TlsClientConnection*>(ConnectionFromSsl(ssl))
      ->delegate_->InsertSession(bssl::UniquePtr<SSL_SESSION>(session));
  return 1;
}

}  // namespace quic
