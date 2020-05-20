// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/tls_server_connection.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

TlsServerConnection::TlsServerConnection(SSL_CTX* ssl_ctx, Delegate* delegate)
    : TlsConnection(ssl_ctx, delegate->ConnectionDelegate()),
      delegate_(delegate) {}

// static
bssl::UniquePtr<SSL_CTX> TlsServerConnection::CreateSslCtx() {
  bssl::UniquePtr<SSL_CTX> ssl_ctx = TlsConnection::CreateSslCtx();
  SSL_CTX_set_tlsext_servername_callback(ssl_ctx.get(),
                                         &SelectCertificateCallback);
  SSL_CTX_set_alpn_select_cb(ssl_ctx.get(), &SelectAlpnCallback, nullptr);
  SSL_CTX_set_options(ssl_ctx.get(), SSL_OP_NO_TICKET);
  return ssl_ctx;
}

void TlsServerConnection::SetCertChain(
    const std::vector<CRYPTO_BUFFER*>& cert_chain) {
  SSL_set_chain_and_key(ssl(), cert_chain.data(), cert_chain.size(), nullptr,
                        &TlsServerConnection::kPrivateKeyMethod);
}

const SSL_PRIVATE_KEY_METHOD TlsServerConnection::kPrivateKeyMethod{
    &TlsServerConnection::PrivateKeySign,
    nullptr,  // decrypt
    &TlsServerConnection::PrivateKeyComplete,
};

// static
TlsServerConnection* TlsServerConnection::ConnectionFromSsl(SSL* ssl) {
  return static_cast<TlsServerConnection*>(
      TlsConnection::ConnectionFromSsl(ssl));
}

// static
int TlsServerConnection::SelectCertificateCallback(SSL* ssl,
                                                   int* out_alert,
                                                   void* /*arg*/) {
  return ConnectionFromSsl(ssl)->delegate_->SelectCertificate(out_alert);
}

// static
int TlsServerConnection::SelectAlpnCallback(SSL* ssl,
                                            const uint8_t** out,
                                            uint8_t* out_len,
                                            const uint8_t* in,
                                            unsigned in_len,
                                            void* /*arg*/) {
  return ConnectionFromSsl(ssl)->delegate_->SelectAlpn(out, out_len, in,
                                                       in_len);
}

// static
ssl_private_key_result_t TlsServerConnection::PrivateKeySign(SSL* ssl,
                                                             uint8_t* out,
                                                             size_t* out_len,
                                                             size_t max_out,
                                                             uint16_t sig_alg,
                                                             const uint8_t* in,
                                                             size_t in_len) {
  return ConnectionFromSsl(ssl)->delegate_->PrivateKeySign(
      out, out_len, max_out, sig_alg,
      quiche::QuicheStringPiece(reinterpret_cast<const char*>(in), in_len));
}

// static
ssl_private_key_result_t TlsServerConnection::PrivateKeyComplete(
    SSL* ssl,
    uint8_t* out,
    size_t* out_len,
    size_t max_out) {
  return ConnectionFromSsl(ssl)->delegate_->PrivateKeyComplete(out, out_len,
                                                               max_out);
}

}  // namespace quic
