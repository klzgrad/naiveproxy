// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/load_balancer/load_balancer_config.h"

#include <cstdint>
#include <cstring>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "openssl/aes.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

namespace {

// Validates all non-key parts of the input.
bool CommonValidation(const uint8_t config_id, const uint8_t server_id_len,
                      const uint8_t nonce_len) {
  if (config_id >= kNumLoadBalancerConfigs || server_id_len == 0 ||
      nonce_len < kLoadBalancerMinNonceLen ||
      nonce_len > kLoadBalancerMaxNonceLen ||
      server_id_len >
          (kQuicMaxConnectionIdWithLengthPrefixLength - nonce_len - 1)) {
    QUIC_BUG(quic_bug_433862549_01)
        << "Invalid LoadBalancerConfig "
        << "Config ID " << static_cast<int>(config_id) << " Server ID Length "
        << static_cast<int>(server_id_len) << " Nonce Length "
        << static_cast<int>(nonce_len);
    return false;
  }
  return true;
}

// Initialize the key in the constructor
absl::optional<AES_KEY> BuildKey(absl::string_view key, bool encrypt) {
  if (key.empty()) {
    return absl::optional<AES_KEY>();
  }
  AES_KEY raw_key;
  if (encrypt) {
    if (AES_set_encrypt_key(reinterpret_cast<const uint8_t *>(key.data()),
                            key.size() * 8, &raw_key) < 0) {
      return absl::optional<AES_KEY>();
    }
  } else if (AES_set_decrypt_key(reinterpret_cast<const uint8_t *>(key.data()),
                                 key.size() * 8, &raw_key) < 0) {
    return absl::optional<AES_KEY>();
  }
  return raw_key;
}

// Functions to handle 4-pass encryption/decryption.
// TakePlaintextFrom{Left,Right}() reads the left or right half of 'from' and
// expands it into a full encryption block ('to') in accordance with the
// internet-draft.
void TakePlaintextFromLeft(const uint8_t *from, const uint8_t plaintext_len,
                           const uint8_t index, uint8_t *to) {
  uint8_t half = plaintext_len / 2;

  to[0] = plaintext_len;
  to[1] = index;
  memcpy(to + 2, from, half);
  if (plaintext_len % 2) {
    to[2 + half] = from[half] & 0xf0;
    half++;
  }
  memset(to + 2 + half, 0, kLoadBalancerBlockSize - 2 - half);
}

void TakePlaintextFromRight(const uint8_t *from, const uint8_t plaintext_len,
                            const uint8_t index, uint8_t *to) {
  uint8_t half = plaintext_len / 2;

  to[0] = plaintext_len;
  to[1] = index;
  memcpy(to + 2, from + half, half + (plaintext_len % 2));
  if (plaintext_len % 2) {
    to[2] &= 0x0f;
    half++;
  }
  memset(to + 2 + half, 0, kLoadBalancerBlockSize - 2 - half);
}

// CiphertextXorWith{Left,Right}() takes the relevant end of the ciphertext in
// 'from' and XORs it with half of the ConnectionId stored at 'to', in
// accordance with the internet-draft.
void CiphertextXorWithLeft(const uint8_t *from, const uint8_t plaintext_len,
                           uint8_t *to) {
  uint8_t half = plaintext_len / 2;
  for (int i = 0; i < half; i++) {
    to[i] ^= from[i];
  }
  if (plaintext_len % 2) {
    to[half] ^= (from[half] & 0xf0);
  }
}

void CiphertextXorWithRight(const uint8_t *from, const uint8_t plaintext_len,
                            uint8_t *to) {
  uint8_t half = plaintext_len / 2;
  int i = 0;
  if (plaintext_len % 2) {
    to[half] ^= (from[0] & 0x0f);
    i++;
  }
  while ((half + i) < plaintext_len) {
    to[half + i] ^= from[i];
    i++;
  }
}

}  // namespace

absl::optional<LoadBalancerConfig> LoadBalancerConfig::Create(
    const uint8_t config_id, const uint8_t server_id_len,
    const uint8_t nonce_len, const absl::string_view key) {
  //  Check for valid parameters.
  if (key.size() != kLoadBalancerKeyLen) {
    QUIC_BUG(quic_bug_433862549_02)
        << "Invalid LoadBalancerConfig Key Length: " << key.size();
    return absl::optional<LoadBalancerConfig>();
  }
  if (!CommonValidation(config_id, server_id_len, nonce_len)) {
    return absl::optional<LoadBalancerConfig>();
  }
  auto new_config =
      LoadBalancerConfig(config_id, server_id_len, nonce_len, key);
  if (!new_config.IsEncrypted()) {
    // Something went wrong in assigning the key!
    QUIC_BUG(quic_bug_433862549_03) << "Something went wrong in initializing "
                                       "the load balancing key.";
    return absl::optional<LoadBalancerConfig>();
  }
  return new_config;
}

// Creates an unencrypted config.
absl::optional<LoadBalancerConfig> LoadBalancerConfig::CreateUnencrypted(
    const uint8_t config_id, const uint8_t server_id_len,
    const uint8_t nonce_len) {
  return CommonValidation(config_id, server_id_len, nonce_len)
             ? LoadBalancerConfig(config_id, server_id_len, nonce_len, "")
             : absl::optional<LoadBalancerConfig>();
}

bool LoadBalancerConfig::EncryptionPass(absl::Span<uint8_t> target,
                                        const uint8_t index) const {
  uint8_t buf[kLoadBalancerBlockSize];
  if (!key_.has_value() || target.size() < plaintext_len()) {
    return false;
  }
  if (index % 2) {  // Odd indices go from left to right
    TakePlaintextFromLeft(target.data(), plaintext_len(), index, buf);
  } else {
    TakePlaintextFromRight(target.data(), plaintext_len(), index, buf);
  }
  if (!BlockEncrypt(buf, buf)) {
    return false;
  }
  // XOR bits over the correct half.
  if (index % 2) {
    CiphertextXorWithRight(buf, plaintext_len(), target.data());
  } else {
    CiphertextXorWithLeft(buf, plaintext_len(), target.data());
  }
  return true;
}

bool LoadBalancerConfig::BlockEncrypt(
    const uint8_t plaintext[kLoadBalancerBlockSize],
    uint8_t ciphertext[kLoadBalancerBlockSize]) const {
  if (!key_.has_value()) {
    return false;
  }
  AES_encrypt(plaintext, ciphertext, &key_.value());
  return true;
}

bool LoadBalancerConfig::BlockDecrypt(
    const uint8_t ciphertext[kLoadBalancerBlockSize],
    uint8_t plaintext[kLoadBalancerBlockSize]) const {
  if (!block_decrypt_key_.has_value()) {
    return false;
  }
  AES_decrypt(ciphertext, plaintext, &block_decrypt_key_.value());
  return true;
}

LoadBalancerConfig::LoadBalancerConfig(const uint8_t config_id,
                                       const uint8_t server_id_len,
                                       const uint8_t nonce_len,
                                       const absl::string_view key)
    : config_id_(config_id),
      server_id_len_(server_id_len),
      nonce_len_(nonce_len),
      key_(BuildKey(key, /* encrypt = */ true)),
      block_decrypt_key_((server_id_len + nonce_len == kLoadBalancerBlockSize)
                             ? BuildKey(key, /* encrypt = */ false)
                             : absl::optional<AES_KEY>()) {}

}  // namespace quic
