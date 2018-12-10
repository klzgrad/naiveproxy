// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/quic_hkdf.h"

#include <memory>

#include "net/third_party/quic/platform/api/quic_logging.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"

namespace quic {

const size_t kSHA256HashLength = 32;
const size_t kMaxKeyMaterialSize = kSHA256HashLength * 256;

QuicHKDF::QuicHKDF(QuicStringPiece secret,
                   QuicStringPiece salt,
                   QuicStringPiece info,
                   size_t key_bytes_to_generate,
                   size_t iv_bytes_to_generate,
                   size_t subkey_secret_bytes_to_generate)
    : QuicHKDF(secret,
               salt,
               info,
               key_bytes_to_generate,
               key_bytes_to_generate,
               iv_bytes_to_generate,
               iv_bytes_to_generate,
               subkey_secret_bytes_to_generate) {}

QuicHKDF::QuicHKDF(QuicStringPiece secret,
                   QuicStringPiece salt,
                   QuicStringPiece info,
                   size_t client_key_bytes_to_generate,
                   size_t server_key_bytes_to_generate,
                   size_t client_iv_bytes_to_generate,
                   size_t server_iv_bytes_to_generate,
                   size_t subkey_secret_bytes_to_generate) {
  const size_t material_length =
      client_key_bytes_to_generate + client_iv_bytes_to_generate +
      server_key_bytes_to_generate + server_iv_bytes_to_generate +
      subkey_secret_bytes_to_generate;
  DCHECK_LT(material_length, kMaxKeyMaterialSize);

  output_.resize(material_length);
  // On Windows, when the size of output_ is zero, dereference of 0'th element
  // results in a crash. C++11 solves this problem by adding a data() getter
  // method to std::vector.
  if (output_.empty()) {
    return;
  }

  ::HKDF(&output_[0], output_.size(), ::EVP_sha256(),
         reinterpret_cast<const uint8_t*>(secret.data()), secret.size(),
         reinterpret_cast<const uint8_t*>(salt.data()), salt.size(),
         reinterpret_cast<const uint8_t*>(info.data()), info.size());

  size_t j = 0;
  if (client_key_bytes_to_generate) {
    client_write_key_ = QuicStringPiece(reinterpret_cast<char*>(&output_[j]),
                                        client_key_bytes_to_generate);
    j += client_key_bytes_to_generate;
  }

  if (server_key_bytes_to_generate) {
    server_write_key_ = QuicStringPiece(reinterpret_cast<char*>(&output_[j]),
                                        server_key_bytes_to_generate);
    j += server_key_bytes_to_generate;
  }

  if (client_iv_bytes_to_generate) {
    client_write_iv_ = QuicStringPiece(reinterpret_cast<char*>(&output_[j]),
                                       client_iv_bytes_to_generate);
    j += client_iv_bytes_to_generate;
  }

  if (server_iv_bytes_to_generate) {
    server_write_iv_ = QuicStringPiece(reinterpret_cast<char*>(&output_[j]),
                                       server_iv_bytes_to_generate);
    j += server_iv_bytes_to_generate;
  }

  if (subkey_secret_bytes_to_generate) {
    subkey_secret_ = QuicStringPiece(reinterpret_cast<char*>(&output_[j]),
                                     subkey_secret_bytes_to_generate);
  }
}

QuicHKDF::~QuicHKDF() {}

}  // namespace quic
