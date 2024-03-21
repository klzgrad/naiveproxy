// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/tls_connection.h"

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

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
      QUIC_BUG(quic_bug_10698_1)
          << "Invalid ssl_encryption_level_t " << static_cast<int>(level);
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
      QUIC_BUG(quic_bug_10698_2)
          << "Invalid encryption level " << static_cast<int>(level);
      return ssl_encryption_initial;
  }
}

TlsConnection::TlsConnection(SSL_CTX* ssl_ctx,
                             TlsConnection::Delegate* delegate,
                             QuicSSLConfig ssl_config)
    : delegate_(delegate),
      ssl_(SSL_new(ssl_ctx)),
      ssl_config_(std::move(ssl_config)) {
  SSL_set_ex_data(
      ssl(), SslIndexSingleton::GetInstance()->ssl_ex_data_index_connection(),
      this);
  if (ssl_config_.early_data_enabled.has_value()) {
    const int early_data_enabled = *ssl_config_.early_data_enabled ? 1 : 0;
    SSL_set_early_data_enabled(ssl(), early_data_enabled);
  }
  if (ssl_config_.signing_algorithm_prefs.has_value()) {
    SSL_set_signing_algorithm_prefs(
        ssl(), ssl_config_.signing_algorithm_prefs->data(),
        ssl_config_.signing_algorithm_prefs->size());
  }
  if (ssl_config_.disable_ticket_support.has_value()) {
    if (*ssl_config_.disable_ticket_support) {
      SSL_set_options(ssl(), SSL_OP_NO_TICKET);
    }
  }
}

void TlsConnection::EnableInfoCallback() {
  SSL_set_info_callback(
      ssl(), +[](const SSL* ssl, int type, int value) {
        ConnectionFromSsl(ssl)->delegate_->InfoCallback(type, value);
      });
}

void TlsConnection::DisableTicketSupport() {
  ssl_config_.disable_ticket_support = true;
  SSL_set_options(ssl(), SSL_OP_NO_TICKET);
}

// static
bssl::UniquePtr<SSL_CTX> TlsConnection::CreateSslCtx() {
  CRYPTO_library_init();
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_with_buffers_method()));
  SSL_CTX_set_min_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_quic_method(ssl_ctx.get(), &kSslQuicMethod);
  SSL_CTX_set_msg_callback(ssl_ctx.get(), &MessageCallback);
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
  TlsConnection::Delegate* delegate = ConnectionFromSsl(ssl)->delegate_;
  if (!delegate->SetReadSecret(QuicEncryptionLevel(level), cipher,
                               absl::MakeSpan(secret, secret_length))) {
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
  TlsConnection::Delegate* delegate = ConnectionFromSsl(ssl)->delegate_;
  delegate->SetWriteSecret(QuicEncryptionLevel(level), cipher,
                           absl::MakeSpan(secret, secret_length));
  return 1;
}

// static
int TlsConnection::WriteMessageCallback(SSL* ssl,
                                        enum ssl_encryption_level_t level,
                                        const uint8_t* data, size_t len) {
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

// static
void TlsConnection::MessageCallback(int is_write, int version, int content_type,
                                    const void* buf, size_t len, SSL* ssl,
                                    void*) {
  ConnectionFromSsl(ssl)->delegate_->MessageCallback(
      is_write != 0, version, content_type,
      absl::string_view(static_cast<const char*>(buf), len));
}

}  // namespace quic
