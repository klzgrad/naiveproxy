// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/tls_handshaker.h"

#include "net/third_party/quic/core/quic_crypto_stream.h"
#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_singleton.h"
#include "third_party/boringssl/src/include/openssl/crypto.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace quic {

namespace {

class SslIndexSingleton {
 public:
  static SslIndexSingleton* GetInstance() {
    return QuicSingleton<SslIndexSingleton>::get();
  }

  int HandshakerIndex() const { return ssl_ex_data_index_handshaker_; }

 private:
  SslIndexSingleton() {
    CRYPTO_library_init();
    ssl_ex_data_index_handshaker_ =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    CHECK_LE(0, ssl_ex_data_index_handshaker_);
  }

  SslIndexSingleton(const SslIndexSingleton&) = delete;
  SslIndexSingleton& operator=(const SslIndexSingleton&) = delete;

  friend QuicSingletonFriend<SslIndexSingleton>;

  int ssl_ex_data_index_handshaker_;
};

}  // namespace

TlsHandshaker::TlsHandshaker(QuicCryptoStream* stream,
                             QuicSession* session,
                             SSL_CTX* ssl_ctx)
    : stream_(stream), session_(session) {
  ssl_.reset(SSL_new(ssl_ctx));
  SSL_set_ex_data(ssl(), SslIndexSingleton::GetInstance()->HandshakerIndex(),
                  this);
}

TlsHandshaker::~TlsHandshaker() {}

bool TlsHandshaker::ProcessInput(QuicStringPiece input, EncryptionLevel level) {
  if (parser_error_ != QUIC_NO_ERROR) {
    return false;
  }
  // TODO(nharper): Call SSL_quic_read_level(ssl()) and check whether the
  // encryption level BoringSSL expects matches the encryption level that we
  // just received input at. If they mismatch, should ProcessInput return true
  // or false? If data is for a future encryption level, it should be queued for
  // later?
  if (SSL_provide_quic_data(ssl(), BoringEncryptionLevel(level),
                            reinterpret_cast<const uint8_t*>(input.data()),
                            input.size()) != 1) {
    // SSL_provide_quic_data can fail for 3 reasons:
    // - API misuse (calling it before SSL_set_custom_quic_method, which we
    //   call in the TlsHandshaker c'tor)
    // - Memory exhaustion when appending data to its buffer
    // - Data provided at the wrong encryption level
    //
    // Of these, the only sensible error to handle is data provided at the wrong
    // encryption level.
    //
    // Note: the error provided below has a good-sounding enum value, although
    // it doesn't match the description as it's a QUIC Crypto specific error.
    parser_error_ = QUIC_INVALID_CRYPTO_MESSAGE_TYPE;
    parser_error_detail_ = "TLS stack failed to receive data";
    return false;
  }
  AdvanceHandshake();
  return true;
}

// static
bssl::UniquePtr<SSL_CTX> TlsHandshaker::CreateSslCtx() {
  CRYPTO_library_init();
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_with_buffers_method()));
  SSL_CTX_set_min_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_quic_method(ssl_ctx.get(), &kSslQuicMethod);
  return ssl_ctx;
}

// static
TlsHandshaker* TlsHandshaker::HandshakerFromSsl(const SSL* ssl) {
  return reinterpret_cast<TlsHandshaker*>(SSL_get_ex_data(
      ssl, SslIndexSingleton::GetInstance()->HandshakerIndex()));
}

// static
EncryptionLevel TlsHandshaker::QuicEncryptionLevel(
    enum ssl_encryption_level_t level) {
  switch (level) {
    case ssl_encryption_initial:
      return ENCRYPTION_NONE;
    case ssl_encryption_early_data:
    case ssl_encryption_handshake:
      return ENCRYPTION_INITIAL;
    case ssl_encryption_application:
      return ENCRYPTION_FORWARD_SECURE;
  }
}

// static
enum ssl_encryption_level_t TlsHandshaker::BoringEncryptionLevel(
    EncryptionLevel level) {
  switch (level) {
    case ENCRYPTION_NONE:
      return ssl_encryption_initial;
    case ENCRYPTION_INITIAL:
      return ssl_encryption_handshake;
    case ENCRYPTION_FORWARD_SECURE:
      return ssl_encryption_application;
    default:
      QUIC_BUG << "Invalid encryption level " << level;
      return ssl_encryption_initial;
  }
}

const EVP_MD* TlsHandshaker::Prf() {
  return EVP_get_digestbynid(
      SSL_CIPHER_get_prf_nid(SSL_get_pending_cipher(ssl())));
}

std::unique_ptr<QuicEncrypter> TlsHandshaker::CreateEncrypter(
    const std::vector<uint8_t>& pp_secret) {
  std::unique_ptr<QuicEncrypter> encrypter =
      QuicEncrypter::CreateFromCipherSuite(
          SSL_CIPHER_get_id(SSL_get_pending_cipher(ssl())));
  CryptoUtils::SetKeyAndIV(Prf(), pp_secret, encrypter.get());
  return encrypter;
}

std::unique_ptr<QuicDecrypter> TlsHandshaker::CreateDecrypter(
    const std::vector<uint8_t>& pp_secret) {
  std::unique_ptr<QuicDecrypter> decrypter =
      QuicDecrypter::CreateFromCipherSuite(
          SSL_CIPHER_get_id(SSL_get_pending_cipher(ssl())));
  CryptoUtils::SetKeyAndIV(Prf(), pp_secret, decrypter.get());
  return decrypter;
}

const SSL_QUIC_METHOD TlsHandshaker::kSslQuicMethod{
    TlsHandshaker::SetEncryptionSecretCallback,
    TlsHandshaker::WriteMessageCallback, TlsHandshaker::FlushFlightCallback,
    TlsHandshaker::SendAlertCallback};

// static
int TlsHandshaker::SetEncryptionSecretCallback(
    SSL* ssl,
    enum ssl_encryption_level_t level,
    const uint8_t* read_key,
    const uint8_t* write_key,
    size_t secret_len) {
  // TODO(nharper): replace these vectors and memcpys with spans (which
  // unfortunately doesn't yet exist in quic/platform/api).
  std::vector<uint8_t> read_secret(secret_len), write_secret(secret_len);
  memcpy(read_secret.data(), read_key, secret_len);
  memcpy(write_secret.data(), write_key, secret_len);
  HandshakerFromSsl(ssl)->SetEncryptionSecret(QuicEncryptionLevel(level),
                                              read_secret, write_secret);
  return 1;
}

// static
int TlsHandshaker::WriteMessageCallback(SSL* ssl,
                                        enum ssl_encryption_level_t level,
                                        const uint8_t* data,
                                        size_t len) {
  HandshakerFromSsl(ssl)->WriteMessage(
      QuicEncryptionLevel(level),
      QuicStringPiece(reinterpret_cast<const char*>(data), len));
  return 1;
}

// static
int TlsHandshaker::FlushFlightCallback(SSL* ssl) {
  HandshakerFromSsl(ssl)->FlushFlight();
  return 1;
}

// static
int TlsHandshaker::SendAlertCallback(SSL* ssl,
                                     enum ssl_encryption_level_t level,
                                     uint8_t desc) {
  HandshakerFromSsl(ssl)->SendAlert(QuicEncryptionLevel(level), desc);
  return 1;
}

void TlsHandshaker::SetEncryptionSecret(
    EncryptionLevel level,
    const std::vector<uint8_t>& read_secret,
    const std::vector<uint8_t>& write_secret) {
  std::unique_ptr<QuicEncrypter> encrypter = CreateEncrypter(write_secret);
  session()->connection()->SetEncrypter(level, std::move(encrypter));
  if (level != ENCRYPTION_FORWARD_SECURE) {
    std::unique_ptr<QuicDecrypter> decrypter = CreateDecrypter(read_secret);
    session()->connection()->SetDecrypter(level, std::move(decrypter));
  } else {
    // When forward-secure read keys are available, they get set as the
    // alternative decrypter instead of the primary decrypter. One reason for
    // this is that after the forward secure keys become available, the server
    // still has crypto handshake messages to read at the handshake encryption
    // level, meaning that both the ENCRYPTION_INITIAL and
    // ENCRYPTION_FORWARD_SECURE decrypters need to be available. (Tests also
    // assume that an alternative decrypter gets set, so at some point we need
    // to call SetAlternativeDecrypter.)
    std::unique_ptr<QuicDecrypter> decrypter = CreateDecrypter(read_secret);
    session()->connection()->SetAlternativeDecrypter(
        level, std::move(decrypter), /*latch_once_used*/ true);
  }
}

void TlsHandshaker::WriteMessage(EncryptionLevel level, QuicStringPiece data) {
  stream_->WriteCryptoData(level, data);
}

void TlsHandshaker::FlushFlight() {}

void TlsHandshaker::SendAlert(EncryptionLevel level, uint8_t desc) {
  // TODO(nharper): Alerts should be sent on the wire as a 16-bit QUIC error
  // code computed to be 0x100 | desc (draft-ietf-quic-tls-14, section 4.8).
  // This puts it in the range reserved for CRYPTO_ERROR
  // (draft-ietf-quic-transport-14, section 11.3). However, according to
  // quic_error_codes.h, this QUIC implementation only sends 1-byte error codes
  // right now.
  CloseConnection(QUIC_HANDSHAKE_FAILED, "TLS handshake failure");
}

}  // namespace quic
