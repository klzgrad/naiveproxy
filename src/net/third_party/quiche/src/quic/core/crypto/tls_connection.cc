// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/crypto/tls_connection.h"

#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_bug_tracker.h"

namespace quic {

namespace {

// BoringSSL allows storing extra data off of some of its data structures,
// including the SSL struct. To allow for multiple callers to store data, each
// caller can use a different index for setting and getting data. These indices
// are globals handed out by calling SSL_get_ex_new_index.
//
// SslIndexSingleton calls SSL_get_ex_new_index on its construction, and then
// provides this index to be used in calls to SSL_get_ex_data/SSL_set_ex_data.
// This is used to store in the SSL struct a pointer to the TlsConnection which
// owns it.
class SslIndexSingleton {
 public:
  static SslIndexSingleton* GetInstance() {
    static SslIndexSingleton* instance = new SslIndexSingleton();
    return instance;
  }

  int ssl_ex_data_index_connection() const {
    return ssl_ex_data_index_connection_;
  }

 private:
  SslIndexSingleton() {
    CRYPTO_library_init();
    ssl_ex_data_index_connection_ =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    QUICHE_CHECK_LE(0, ssl_ex_data_index_connection_);
  }

  SslIndexSingleton(const SslIndexSingleton&) = delete;
  SslIndexSingleton& operator=(const SslIndexSingleton&) = delete;

  // The index to supply to SSL_get_ex_data/SSL_set_ex_data for getting/setting
  // the TlsConnection pointer.
  int ssl_ex_data_index_connection_;
};

}  // namespace

// static
EncryptionLevel TlsConnection::QuicEncryptionLevel(
    enum ssl_encryption_level_t level) {
  switch (level) {
    case ssl_encryption_initial:
      return ENCRYPTION_INITIAL;
    case ssl_encryption_early_data:
      return ENCRYPTION_ZERO_RTT;
    case ssl_encryption_handshake:
      return ENCRYPTION_HANDSHAKE;
    case ssl_encryption_application:
      return ENCRYPTION_FORWARD_SECURE;
    default:
      QUIC_BUG << "Invalid ssl_encryption_level_t " << static_cast<int>(level);
      return ENCRYPTION_INITIAL;
  }
}

// static
enum ssl_encryption_level_t TlsConnection::BoringEncryptionLevel(
    EncryptionLevel level) {
  switch (level) {
    case ENCRYPTION_INITIAL:
      return ssl_encryption_initial;
    case ENCRYPTION_HANDSHAKE:
      return ssl_encryption_handshake;
    case ENCRYPTION_ZERO_RTT:
      return ssl_encryption_early_data;
    case ENCRYPTION_FORWARD_SECURE:
      return ssl_encryption_application;
    default:
      QUIC_BUG << "Invalid encryption level " << static_cast<int>(level);
      return ssl_encryption_initial;
  }
}

TlsConnection::TlsConnection(SSL_CTX* ssl_ctx,
                             TlsConnection::Delegate* delegate)
    : delegate_(delegate), ssl_(SSL_new(ssl_ctx)) {
  SSL_set_ex_data(
      ssl(), SslIndexSingleton::GetInstance()->ssl_ex_data_index_connection(),
      this);
}
// static
bssl::UniquePtr<SSL_CTX> TlsConnection::CreateSslCtx(int cert_verify_mode) {
  CRYPTO_library_init();
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_with_buffers_method()));
  SSL_CTX_set_min_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_quic_method(ssl_ctx.get(), &kSslQuicMethod);
  if (cert_verify_mode != SSL_VERIFY_NONE) {
    SSL_CTX_set_custom_verify(ssl_ctx.get(), cert_verify_mode, &VerifyCallback);
  }
  return ssl_ctx;
}

// static
TlsConnection* TlsConnection::ConnectionFromSsl(const SSL* ssl) {
  return reinterpret_cast<TlsConnection*>(SSL_get_ex_data(
      ssl, SslIndexSingleton::GetInstance()->ssl_ex_data_index_connection()));
}

// static
enum ssl_verify_result_t TlsConnection::VerifyCallback(SSL* ssl,
                                                       uint8_t* out_alert) {
  return ConnectionFromSsl(ssl)->delegate_->VerifyCert(out_alert);
}

const SSL_QUIC_METHOD TlsConnection::kSslQuicMethod{
    TlsConnection::SetReadSecretCallback, TlsConnection::SetWriteSecretCallback,
    TlsConnection::WriteMessageCallback, TlsConnection::FlushFlightCallback,
    TlsConnection::SendAlertCallback};

// static
int TlsConnection::SetReadSecretCallback(SSL* ssl,
                                         enum ssl_encryption_level_t level,
                                         const SSL_CIPHER* cipher,
                                         const uint8_t* secret,
                                         size_t secret_length) {
  // TODO(nharper): replace this vector with a span (which unfortunately doesn't
  // yet exist in quic/platform/api).
  std::vector<uint8_t> secret_vec(secret, secret + secret_length);
  TlsConnection::Delegate* delegate = ConnectionFromSsl(ssl)->delegate_;
  if (!delegate->SetReadSecret(QuicEncryptionLevel(level), cipher,
                               secret_vec)) {
    return 0;
  }
  return 1;
}

// static
int TlsConnection::SetWriteSecretCallback(SSL* ssl,
                                          enum ssl_encryption_level_t level,
                                          const SSL_CIPHER* cipher,
                                          const uint8_t* secret,
                                          size_t secret_length) {
  // TODO(nharper): replace this vector with a span (which unfortunately doesn't
  // yet exist in quic/platform/api).
  std::vector<uint8_t> secret_vec(secret, secret + secret_length);
  TlsConnection::Delegate* delegate = ConnectionFromSsl(ssl)->delegate_;
  delegate->SetWriteSecret(QuicEncryptionLevel(level), cipher, secret_vec);
  return 1;
}

// static
int TlsConnection::WriteMessageCallback(SSL* ssl,
                                        enum ssl_encryption_level_t level,
                                        const uint8_t* data,
                                        size_t len) {
  ConnectionFromSsl(ssl)->delegate_->WriteMessage(
      QuicEncryptionLevel(level),
      absl::string_view(reinterpret_cast<const char*>(data), len));
  return 1;
}

// static
int TlsConnection::FlushFlightCallback(SSL* ssl) {
  ConnectionFromSsl(ssl)->delegate_->FlushFlight();
  return 1;
}

// static
int TlsConnection::SendAlertCallback(SSL* ssl,
                                     enum ssl_encryption_level_t level,
                                     uint8_t desc) {
  ConnectionFromSsl(ssl)->delegate_->SendAlert(QuicEncryptionLevel(level),
                                               desc);
  return 1;
}

}  // namespace quic
