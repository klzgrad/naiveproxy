// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/tls_handshaker.h"

#include "net/third_party/quic/core/quic_crypto_stream.h"
#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_singleton.h"
#include "third_party/boringssl/src/include/openssl/crypto.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace quic {

namespace {

const char kClientLabel[] = "EXPORTER-QUIC client 1-RTT Secret";
const char kServerLabel[] = "EXPORTER-QUIC server 1-RTT Secret";

}  // namespace

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

// static
TlsHandshaker* TlsHandshaker::HandshakerFromSsl(const SSL* ssl) {
  return reinterpret_cast<TlsHandshaker*>(SSL_get_ex_data(
      ssl, SslIndexSingleton::GetInstance()->HandshakerIndex()));
}

const EVP_MD* TlsHandshaker::Prf() {
  return EVP_get_digestbynid(
      SSL_CIPHER_get_prf_nid(SSL_get_current_cipher(ssl())));
}

bool TlsHandshaker::DeriveSecrets(std::vector<uint8_t>* client_secret_out,
                                  std::vector<uint8_t>* server_secret_out) {
  size_t hash_len = EVP_MD_size(Prf());
  client_secret_out->resize(hash_len);
  server_secret_out->resize(hash_len);
  return (SSL_export_keying_material(
              ssl(), client_secret_out->data(), hash_len, kClientLabel,
              QUIC_ARRAYSIZE(kClientLabel) - 1, nullptr, 0, 0) == 1) &&
         (SSL_export_keying_material(
              ssl(), server_secret_out->data(), hash_len, kServerLabel,
              QUIC_ARRAYSIZE(kServerLabel) - 1, nullptr, 0, 0) == 1);
}

std::unique_ptr<QuicEncrypter> TlsHandshaker::CreateEncrypter(
    const std::vector<uint8_t>& pp_secret) {
  std::unique_ptr<QuicEncrypter> encrypter =
      QuicEncrypter::CreateFromCipherSuite(
          SSL_CIPHER_get_id(SSL_get_current_cipher(ssl())));
  CryptoUtils::SetKeyAndIV(Prf(), pp_secret, encrypter.get());
  return encrypter;
}

std::unique_ptr<QuicDecrypter> TlsHandshaker::CreateDecrypter(
    const std::vector<uint8_t>& pp_secret) {
  std::unique_ptr<QuicDecrypter> decrypter =
      QuicDecrypter::CreateFromCipherSuite(
          SSL_CIPHER_get_id(SSL_get_current_cipher(ssl())));
  CryptoUtils::SetKeyAndIV(Prf(), pp_secret, decrypter.get());
  return decrypter;
}

// static
bssl::UniquePtr<SSL_CTX> TlsHandshaker::CreateSslCtx() {
  CRYPTO_library_init();
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_with_buffers_method()));
  SSL_CTX_set_min_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
  return ssl_ctx;
}

TlsHandshaker::TlsHandshaker(QuicCryptoStream* stream,
                             QuicSession* session,
                             SSL_CTX* ssl_ctx)
    : stream_(stream), session_(session), bio_adapter_(this) {
  ssl_.reset(SSL_new(ssl_ctx));
  SSL_set_ex_data(ssl(), SslIndexSingleton::GetInstance()->HandshakerIndex(),
                  this);

  // Set BIO for ssl_.
  BIO* bio = bio_adapter_.bio();
  BIO_up_ref(bio);
  SSL_set0_rbio(ssl(), bio);
  BIO_up_ref(bio);
  SSL_set0_wbio(ssl(), bio);
}

TlsHandshaker::~TlsHandshaker() {}

void TlsHandshaker::OnDataAvailableForBIO() {
  AdvanceHandshake();
}

void TlsHandshaker::OnDataReceivedFromBIO(const QuicStringPiece& data) {
  // TODO(nharper): Call NeuterUnencryptedPackets when encryption keys are set.
  stream_->WriteCryptoData(data);
}

CryptoMessageParser* TlsHandshaker::crypto_message_parser() {
  return &bio_adapter_;
}

}  // namespace quic
