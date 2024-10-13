// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/tls_server_connection.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/tls_connection.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

TlsServerConnection::TlsServerConnection(SSL_CTX* ssl_ctx, Delegate* delegate,
                                         QuicSSLConfig ssl_config)
    : TlsConnection(ssl_ctx, delegate->ConnectionDelegate(),
                    std::move(ssl_config)),
      delegate_(delegate) {
  // By default, cert verify callback is not installed on ssl(), so only need to
  // UpdateCertVerifyCallback() if client_cert_mode is not kNone.
  if (TlsConnection::ssl_config().client_cert_mode != ClientCertMode::kNone) {
    UpdateCertVerifyCallback();
  }
}

// static
bssl::UniquePtr<SSL_CTX> TlsServerConnection::CreateSslCtx(
    ProofSource* proof_source) {
  bssl::UniquePtr<SSL_CTX> ssl_ctx = TlsConnection::CreateSslCtx();

  // Server does not request/verify client certs by default. Individual server
  // connections may call SSL_set_custom_verify on their SSL object to request
  // client certs.

  SSL_CTX_set_tlsext_servername_callback(ssl_ctx.get(),
                                         &TlsExtServernameCallback);
  SSL_CTX_set_alpn_select_cb(ssl_ctx.get(), &SelectAlpnCallback, nullptr);
  // We don't actually need the TicketCrypter here, but we need to know
  // whether it's set.
  if (proof_source->GetTicketCrypter()) {
    QUIC_CODE_COUNT(quic_session_tickets_enabled);
    SSL_CTX_set_ticket_aead_method(ssl_ctx.get(),
                                   &TlsServerConnection::kSessionTicketMethod);
  } else {
    QUIC_CODE_COUNT(quic_session_tickets_disabled);
  }

  SSL_CTX_set_early_data_enabled(ssl_ctx.get(), 1);

  SSL_CTX_set_select_certificate_cb(
      ssl_ctx.get(), &TlsServerConnection::EarlySelectCertCallback);
  SSL_CTX_set_options(ssl_ctx.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);

  // Allow ProofSource to change SSL_CTX settings.
  proof_source->OnNewSslCtx(ssl_ctx.get());

  return ssl_ctx;
}

absl::Status TlsServerConnection::ConfigureSSL(
    ProofSourceHandleCallback::ConfigureSSLFunc configure_ssl) {
  return std::move(configure_ssl)(*ssl(),  // never nullptr
                                  TlsServerConnection::kPrivateKeyMethod);
}

void TlsServerConnection::SetCertChain(
    const std::vector<CRYPTO_BUFFER*>& cert_chain) {
  SSL_set_chain_and_key(ssl(), cert_chain.data(), cert_chain.size(), nullptr,
                        &TlsServerConnection::kPrivateKeyMethod);
}

void TlsServerConnection::SetClientCertMode(ClientCertMode client_cert_mode) {
  if (ssl_config().client_cert_mode == client_cert_mode) {
    return;
  }

  mutable_ssl_config().client_cert_mode = client_cert_mode;
  UpdateCertVerifyCallback();
}

void TlsServerConnection::UpdateCertVerifyCallback() {
  const ClientCertMode client_cert_mode = ssl_config().client_cert_mode;
  if (client_cert_mode == ClientCertMode::kNone) {
    SSL_set_custom_verify(ssl(), SSL_VERIFY_NONE, nullptr);
    return;
  }

  int mode = SSL_VERIFY_PEER;
  if (client_cert_mode == ClientCertMode::kRequire) {
    mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  } else {
    QUICHE_DCHECK_EQ(client_cert_mode, ClientCertMode::kRequest);
  }
  SSL_set_custom_verify(ssl(), mode, &VerifyCallback);
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
ssl_select_cert_result_t TlsServerConnection::EarlySelectCertCallback(
    const SSL_CLIENT_HELLO* client_hello) {
  return ConnectionFromSsl(client_hello->ssl)
      ->delegate_->EarlySelectCertCallback(client_hello);
}

// static
int TlsServerConnection::TlsExtServernameCallback(SSL* ssl, int* out_alert,
                                                  void* /*arg*/) {
  return ConnectionFromSsl(ssl)->delegate_->TlsExtServernameCallback(out_alert);
}

// static
int TlsServerConnection::SelectAlpnCallback(SSL* ssl, const uint8_t** out,
                                            uint8_t* out_len, const uint8_t* in,
                                            unsigned in_len, void* /*arg*/) {
  return ConnectionFromSsl(ssl)->delegate_->SelectAlpn(out, out_len, in,
                                                       in_len);
}

// static
ssl_private_key_result_t TlsServerConnection::PrivateKeySign(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out, uint16_t sig_alg,
    const uint8_t* in, size_t in_len) {
  return ConnectionFromSsl(ssl)->delegate_->PrivateKeySign(
      out, out_len, max_out, sig_alg,
      absl::string_view(reinterpret_cast<const char*>(in), in_len));
}

// static
ssl_private_key_result_t TlsServerConnection::PrivateKeyComplete(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out) {
  return ConnectionFromSsl(ssl)->delegate_->PrivateKeyComplete(out, out_len,
                                                               max_out);
}

// static
const SSL_TICKET_AEAD_METHOD TlsServerConnection::kSessionTicketMethod{
    TlsServerConnection::SessionTicketMaxOverhead,
    TlsServerConnection::SessionTicketSeal,
    TlsServerConnection::SessionTicketOpen,
};

// static
size_t TlsServerConnection::SessionTicketMaxOverhead(SSL* ssl) {
  return ConnectionFromSsl(ssl)->delegate_->SessionTicketMaxOverhead();
}

// static
int TlsServerConnection::SessionTicketSeal(SSL* ssl, uint8_t* out,
                                           size_t* out_len, size_t max_out_len,
                                           const uint8_t* in, size_t in_len) {
  return ConnectionFromSsl(ssl)->delegate_->SessionTicketSeal(
      out, out_len, max_out_len,
      absl::string_view(reinterpret_cast<const char*>(in), in_len));
}

// static
enum ssl_ticket_aead_result_t TlsServerConnection::SessionTicketOpen(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out_len,
    const uint8_t* in, size_t in_len) {
  return ConnectionFromSsl(ssl)->delegate_->SessionTicketOpen(
      out, out_len, max_out_len,
      absl::string_view(reinterpret_cast<const char*>(in), in_len));
}

}  // namespace quic
