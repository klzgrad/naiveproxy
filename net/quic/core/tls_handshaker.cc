// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/tls_handshaker.h"

#include "base/memory/singleton.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace net {

namespace {

const char kClientLabel[] = "EXPORTER-QUIC client 1-RTT Secret";
const char kServerLabel[] = "EXPORTER-QUIC server 1-RTT Secret";

}  // namespace

// static
bool TlsHandshaker::DeriveSecrets(SSL* ssl,
                                  std::vector<uint8_t>* client_secret_out,
                                  std::vector<uint8_t>* server_secret_out) {
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl);
  if (cipher == nullptr) {
    return false;
  }
  const EVP_MD* prf = EVP_get_digestbynid(SSL_CIPHER_get_prf_nid(cipher));
  if (prf == nullptr) {
    return false;
  }
  size_t hash_len = EVP_MD_size(prf);
  client_secret_out->resize(hash_len);
  server_secret_out->resize(hash_len);
  return SSL_export_keying_material(ssl, client_secret_out->data(), hash_len,
                                    kClientLabel, arraysize(kClientLabel),
                                    nullptr, 0, 0) &&
         SSL_export_keying_material(ssl, server_secret_out->data(), hash_len,
                                    kServerLabel, arraysize(kServerLabel),
                                    nullptr, 0, 0);
}

namespace {

class SslIndexSingleton {
 public:
  static SslIndexSingleton* GetInstance() {
    return base::Singleton<SslIndexSingleton>::get();
  }

  int HandshakerIndex() { return ssl_ex_data_index_handshaker_; }

 private:
  SslIndexSingleton() {
    ssl_ex_data_index_handshaker_ =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    CHECK_LE(0, ssl_ex_data_index_handshaker_);
  }

  friend struct base::DefaultSingletonTraits<SslIndexSingleton>;

  int ssl_ex_data_index_handshaker_;

  DISALLOW_COPY_AND_ASSIGN(SslIndexSingleton);
};

}  // namespace

// static
TlsHandshaker* TlsHandshaker::HandshakerFromSsl(const SSL* ssl) {
  return reinterpret_cast<TlsHandshaker*>(SSL_get_ex_data(
      ssl, SslIndexSingleton::GetInstance()->HandshakerIndex()));
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

}  // namespace net
