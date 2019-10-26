// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/tls_handshaker.h"

#include "third_party/boringssl/src/include/openssl/crypto.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"

namespace quic {

TlsHandshaker::TlsHandshaker(QuicCryptoStream* stream,
                             QuicSession* session,
                             SSL_CTX* /*ssl_ctx*/)
    : stream_(stream), session_(session) {
  QUIC_BUG_IF(!GetQuicFlag(FLAGS_quic_supports_tls_handshake))
      << "Attempted to create TLS handshaker when TLS is disabled";
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
  if (SSL_provide_quic_data(ssl(), TlsConnection::BoringEncryptionLevel(level),
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

size_t TlsHandshaker::BufferSizeLimitForLevel(EncryptionLevel level) const {
  return SSL_quic_max_handshake_flight_len(
      ssl(), TlsConnection::BoringEncryptionLevel(level));
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

void TlsHandshaker::SetEncryptionSecret(
    EncryptionLevel level,
    const std::vector<uint8_t>& read_secret,
    const std::vector<uint8_t>& write_secret) {
  std::unique_ptr<QuicEncrypter> encrypter = CreateEncrypter(write_secret);
  session()->connection()->SetEncrypter(level, std::move(encrypter));
  std::unique_ptr<QuicDecrypter> decrypter = CreateDecrypter(read_secret);
  session()->connection()->InstallDecrypter(level, std::move(decrypter));
}

void TlsHandshaker::WriteMessage(EncryptionLevel level, QuicStringPiece data) {
  stream_->WriteCryptoData(level, data);
}

void TlsHandshaker::FlushFlight() {}

void TlsHandshaker::SendAlert(EncryptionLevel /*level*/, uint8_t desc) {
  // TODO(nharper): Alerts should be sent on the wire as a 16-bit QUIC error
  // code computed to be 0x100 | desc (draft-ietf-quic-tls-14, section 4.8).
  // This puts it in the range reserved for CRYPTO_ERROR
  // (draft-ietf-quic-transport-14, section 11.3). However, according to
  // quic_error_codes.h, this QUIC implementation only sends 1-byte error codes
  // right now.
  QUIC_DLOG(INFO) << "TLS failing handshake due to alert "
                  << static_cast<int>(desc);
  CloseConnection(QUIC_HANDSHAKE_FAILED, "TLS handshake failure");
}

}  // namespace quic
