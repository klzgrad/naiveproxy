// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/aes_base_encrypter.h"

#include "absl/strings/string_view.h"
#include "openssl/aes.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

bool AesBaseEncrypter::SetHeaderProtectionKey(absl::string_view key) {
  if (key.size() != GetKeySize()) {
    QUIC_BUG(quic_bug_10726_1)
        << "Invalid key size for header protection: " << key.size();
    return false;
  }
  if (AES_set_encrypt_key(reinterpret_cast<const uint8_t*>(key.data()),
                          key.size() * 8, &pne_key_) != 0) {
    QUIC_BUG(quic_bug_10726_2) << "Unexpected failure of AES_set_encrypt_key";
    return false;
  }
  return true;
}

std::string AesBaseEncrypter::GenerateHeaderProtectionMask(
    absl::string_view sample) {
  if (sample.size() != AES_BLOCK_SIZE) {
    return std::string();
  }
  std::string out(AES_BLOCK_SIZE, 0);
  AES_encrypt(reinterpret_cast<const uint8_t*>(sample.data()),
              reinterpret_cast<uint8_t*>(const_cast<char*>(out.data())),
              &pne_key_);
  return out;
}

QuicPacketCount AesBaseEncrypter::GetConfidentialityLimit() const {
  // For AEAD_AES_128_GCM and AEAD_AES_256_GCM ... endpoints that do not send
  // packets larger than 2^11 bytes cannot protect more than 2^28 packets.
  // https://quicwg.org/base-drafts/draft-ietf-quic-tls.html#name-confidentiality-limit
  static_assert(kMaxOutgoingPacketSize <= 2048,
                "This key limit requires limits on encryption payload sizes");
  return 268435456U;
}

}  // namespace quic
