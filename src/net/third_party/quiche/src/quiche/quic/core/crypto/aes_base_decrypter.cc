// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/aes_base_decrypter.h"

#include <string>

#include "absl/strings/string_view.h"
#include "openssl/aes.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

bool AesBaseDecrypter::SetHeaderProtectionKey(absl::string_view key) {
  if (key.size() != GetKeySize()) {
    QUIC_BUG(quic_bug_10649_1) << "Invalid key size for header protection";
    return false;
  }
  if (AES_set_encrypt_key(reinterpret_cast<const uint8_t*>(key.data()),
                          key.size() * 8, &pne_key_) != 0) {
    QUIC_BUG(quic_bug_10649_2) << "Unexpected failure of AES_set_encrypt_key";
    return false;
  }
  return true;
}

std::string AesBaseDecrypter::GenerateHeaderProtectionMask(
    QuicDataReader* sample_reader) {
  absl::string_view sample;
  if (!sample_reader->ReadStringPiece(&sample, AES_BLOCK_SIZE)) {
    return std::string();
  }
  std::string out(AES_BLOCK_SIZE, 0);
  AES_encrypt(reinterpret_cast<const uint8_t*>(sample.data()),
              reinterpret_cast<uint8_t*>(const_cast<char*>(out.data())),
              &pne_key_);
  return out;
}

QuicPacketCount AesBaseDecrypter::GetIntegrityLimit() const {
  // For AEAD_AES_128_GCM ... endpoints that do not attempt to remove
  // protection from packets larger than 2^11 bytes can attempt to remove
  // protection from at most 2^57 packets.
  // For AEAD_AES_256_GCM [the limit] is substantially larger than the limit for
  // AEAD_AES_128_GCM. However, this document recommends that the same limit be
  // applied to both functions as either limit is acceptably large.
  // https://quicwg.org/base-drafts/draft-ietf-quic-tls.html#name-integrity-limit
  static_assert(kMaxIncomingPacketSize <= 2048,
                "This key limit requires limits on decryption payload sizes");
  return 144115188075855872U;
}

}  // namespace quic
