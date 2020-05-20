// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_base_decrypter.h"

#include "third_party/boringssl/src/include/openssl/aes.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

bool AesBaseDecrypter::SetHeaderProtectionKey(quiche::QuicheStringPiece key) {
  if (key.size() != GetKeySize()) {
    QUIC_BUG << "Invalid key size for header protection";
    return false;
  }
  if (AES_set_encrypt_key(reinterpret_cast<const uint8_t*>(key.data()),
                          key.size() * 8, &pne_key_) != 0) {
    QUIC_BUG << "Unexpected failure of AES_set_encrypt_key";
    return false;
  }
  return true;
}

std::string AesBaseDecrypter::GenerateHeaderProtectionMask(
    QuicDataReader* sample_reader) {
  quiche::QuicheStringPiece sample;
  if (!sample_reader->ReadStringPiece(&sample, AES_BLOCK_SIZE)) {
    return std::string();
  }
  std::string out(AES_BLOCK_SIZE, 0);
  AES_encrypt(reinterpret_cast<const uint8_t*>(sample.data()),
              reinterpret_cast<uint8_t*>(const_cast<char*>(out.data())),
              &pne_key_);
  return out;
}

}  // namespace quic
