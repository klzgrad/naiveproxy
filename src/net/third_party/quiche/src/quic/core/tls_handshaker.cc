// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/tls_handshaker.h"

#include "third_party/boringssl/src/include/openssl/crypto.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

TlsHandshaker::TlsHandshaker(QuicCryptoStream* stream, QuicSession* session)
    : stream_(stream), handshaker_delegate_(session) {}

TlsHandshaker::~TlsHandshaker() {}

bool TlsHandshaker::ProcessInput(quiche::QuicheStringPiece input,
                                 EncryptionLevel level) {
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

const EVP_MD* TlsHandshaker::Prf(const SSL_CIPHER* cipher) {
  return EVP_get_digestbynid(SSL_CIPHER_get_prf_nid(cipher));
}

void TlsHandshaker::SetWriteSecret(EncryptionLevel level,
                                   const SSL_CIPHER* cipher,
                                   const std::vector<uint8_t>& write_secret) {
  std::unique_ptr<QuicEncrypter> encrypter =
      QuicEncrypter::CreateFromCipherSuite(SSL_CIPHER_get_id(cipher));
  CryptoUtils::SetKeyAndIV(Prf(cipher), write_secret, encrypter.get());
  handshaker_delegate_->OnNewEncryptionKeyAvailable(level,
                                                    std::move(encrypter));
}

bool TlsHandshaker::SetReadSecret(EncryptionLevel level,
                                  const SSL_CIPHER* cipher,
                                  const std::vector<uint8_t>& read_secret) {
  std::unique_ptr<QuicDecrypter> decrypter =
      QuicDecrypter::CreateFromCipherSuite(SSL_CIPHER_get_id(cipher));
  CryptoUtils::SetKeyAndIV(Prf(cipher), read_secret, decrypter.get());
  return handshaker_delegate_->OnNewDecryptionKeyAvailable(
      level, std::move(decrypter),
      /*set_alternative_decrypter=*/false,
      /*latch_once_used=*/false);
}

void TlsHandshaker::WriteMessage(EncryptionLevel level,
                                 quiche::QuicheStringPiece data) {
  stream_->WriteCryptoData(level, data);
}

void TlsHandshaker::FlushFlight() {}

void TlsHandshaker::SendAlert(EncryptionLevel level, uint8_t desc) {
  // TODO(b/151676147): Alerts should be sent on the wire as a varint QUIC error
  // code computed to be 0x100 | desc (draft-ietf-quic-tls-27, section 4.9).
  // This puts it in the range reserved for CRYPTO_ERROR
  // (draft-ietf-quic-transport-27, section 20). However, according to
  // quic_error_codes.h, this QUIC implementation only sends 1-byte error codes
  // right now.
  std::string error_details = quiche::QuicheStrCat(
      "TLS handshake failure (", EncryptionLevelToString(level), ") ",
      static_cast<int>(desc), ": ", SSL_alert_desc_string_long(desc));
  QUIC_DLOG(ERROR) << error_details;
  CloseConnection(QUIC_HANDSHAKE_FAILED, error_details);
}

}  // namespace quic
