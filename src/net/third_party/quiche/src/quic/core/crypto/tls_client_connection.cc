// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/tls_client_connection.h"

namespace quic {

TlsClientConnection::TlsClientConnection(SSL_CTX* ssl_ctx, Delegate* delegate)
    : TlsConnection(ssl_ctx, delegate->ConnectionDelegate()),
      delegate_(delegate) {}

// static
bssl::UniquePtr<SSL_CTX> TlsClientConnection::CreateSslCtx() {
  bssl::UniquePtr<SSL_CTX> ssl_ctx = TlsConnection::CreateSslCtx();
  // Configure certificate verification.
  // TODO(nharper): This only verifies certs on initial connection, not on
  // resumption. Chromium has this callback be a no-op and verifies the
  // certificate after the connection is complete. We need to re-verify on
  // resumption in case of expiration or revocation/distrust.
  SSL_CTX_set_custom_verify(ssl_ctx.get(), SSL_VERIFY_PEER, &VerifyCallback);
  return ssl_ctx;
}

// static
enum ssl_verify_result_t TlsClientConnection::VerifyCallback(
    SSL* ssl,
    uint8_t* out_alert) {
  return static_cast<TlsClientConnection*>(ConnectionFromSsl(ssl))
      ->delegate_->VerifyCert(out_alert);
}

}  // namespace quic
