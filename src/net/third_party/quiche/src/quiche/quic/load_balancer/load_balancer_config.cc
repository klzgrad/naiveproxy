// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/load_balancer/load_balancer_config.h"

#include <cstdint>
#include <cstring>
#include <optional>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "openssl/aes.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/load_balancer/load_balancer_server_id.h"
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
std::optional<AES_KEY> BuildKey(absl::string_view key, bool encrypt) {
  if (key.empty()) {
    return std::optional<AES_KEY>();
  }
  AES_KEY raw_key;
  if (encrypt) {
    if (AES_set_encrypt_key(reinterpret_cast<const uint8_t *>(key.data()),
                            key.size() * 8, &raw_key) < 0) {
      return std::optional<AES_KEY>();
    }
  } else if (AES_set_decrypt_key(reinterpret_cast<const uint8_t *>(key.data()),
                                 key.size() * 8, &raw_key) < 0) {
    return std::optional<AES_KEY>();
  }
  return raw_key;
}

}  // namespace

std::optional<LoadBalancerConfig> LoadBalancerConfig::Create(
    const uint8_t config_id, const uint8_t server_id_len,
    const uint8_t nonce_len, const absl::string_view key) {
  //  Check for valid parameters.
  if (key.size() != kLoadBalancerKeyLen) {
    QUIC_BUG(quic_bug_433862549_02)
        << "Invalid LoadBalancerConfig Key Length: " << key.size();
    return std::optional<LoadBalancerConfig>();
  }
  if (!CommonValidation(config_id, server_id_len, nonce_len)) {
    return std::optional<LoadBalancerConfig>();
  }
  auto new_config =
      LoadBalancerConfig(config_id, server_id_len, nonce_len, key);
  if (!new_config.IsEncrypted()) {
    // Something went wrong in assigning the key!
    QUIC_BUG(quic_bug_433862549_03) << "Something went wrong in initializing "
                                       "the load balancing key.";
    return std::optional<LoadBalancerConfig>();
  }
  return new_config;
}

// Creates an unencrypted config.
std::optional<LoadBalancerConfig> LoadBalancerConfig::CreateUnencrypted(
    const uint8_t config_id, const uint8_t server_id_len,
    const uint8_t nonce_len) {
  return CommonValidation(config_id, server_id_len, nonce_len)
             ? LoadBalancerConfig(config_id, server_id_len, nonce_len, "")
             : std::optional<LoadBalancerConfig>();
}

// Note that |ciphertext| does not include the first byte of the connection ID.
bool LoadBalancerConfig::FourPassDecrypt(
    absl::Span<const uint8_t> ciphertext,
    LoadBalancerServerId& server_id) const {
  if (ciphertext.size() < plaintext_len()) {
    QUIC_BUG(quic_bug_599862571_02)
        << "Called FourPassDecrypt with a short Connection ID";
    return false;
  }
  if (!key_.has_value()) {
    return false;
  }
  // Do 3 or 4 passes. Only 3 are necessary if the server_id is short enough
  // to fit in the first half of the connection ID (the decoder doesn't need
  // to extract the nonce).
  uint8_t* left = server_id.mutable_data();
  uint8_t right[kLoadBalancerBlockSize];
  uint8_t half_len;  // half the length of the plaintext, rounded up
  bool is_length_odd =
      InitializeFourPass(ciphertext.data(), left, right, &half_len);
  uint8_t end_index = (server_id_len_ > nonce_len_) ? 1 : 2;
  for (uint8_t index = kNumLoadBalancerCryptoPasses; index >= end_index;
       --index) {
    // Encrypt left/right and xor the result with right/left, respectively.
    EncryptionPass(index, half_len, is_length_odd, left, right);
  }
  // Consolidate left and right into a server ID with minimum copying.
  if (server_id_len_ < half_len ||
      (server_id_len_ == half_len && !is_length_odd)) {
    // There is no half-byte to handle. Server ID is already written in to
    // server_id.
    return true;
  }
  if (is_length_odd) {
    right[0] |= *(left + --half_len);  // Combine the halves of the odd byte.
  }
  memcpy(server_id.mutable_data() + half_len, right, server_id_len_ - half_len);
  return true;
}

// Note that |plaintext| includes the first byte of the connection ID.
QuicConnectionId LoadBalancerConfig::FourPassEncrypt(
    absl::Span<uint8_t> plaintext) const {
  if (plaintext.size() < total_len()) {
    QUIC_BUG(quic_bug_599862571_03)
        << "Called FourPassEncrypt with a short Connection ID";
    return QuicConnectionId();
  }
  if (!key_.has_value()) {
    return QuicConnectionId();
  }
  uint8_t left[kLoadBalancerBlockSize];
  uint8_t right[kLoadBalancerBlockSize];
  uint8_t half_len;  // half the length of the plaintext, rounded up
  bool is_length_odd =
      InitializeFourPass(plaintext.data() + 1, left, right, &half_len);
  for (uint8_t index = 1; index <= kNumLoadBalancerCryptoPasses; ++index) {
    EncryptionPass(index, half_len, is_length_odd, left, right);
  }
  // Consolidate left and right into a server ID with minimum copying.
  if (is_length_odd) {
    // Combine the halves of the odd byte.
    right[0] |= left[--half_len];
  }
  memcpy(plaintext.data() + 1, left, half_len);
  memcpy(plaintext.data() + half_len + 1, right, plaintext_len() - half_len);
  return QuicConnectionId(reinterpret_cast<char*>(plaintext.data()),
                          total_len());
}

bool LoadBalancerConfig::BlockEncrypt(
    const uint8_t plaintext[kLoadBalancerBlockSize],
    uint8_t ciphertext[kLoadBalancerBlockSize]) const {
  if (!key_.has_value()) {
    return false;
  }
  AES_encrypt(plaintext, ciphertext, &*key_);
  return true;
}

bool LoadBalancerConfig::BlockDecrypt(
    const uint8_t ciphertext[kLoadBalancerBlockSize],
    uint8_t plaintext[kLoadBalancerBlockSize]) const {
  if (!block_decrypt_key_.has_value()) {
    return false;
  }
  AES_decrypt(ciphertext, plaintext, &*block_decrypt_key_);
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
                             : std::optional<AES_KEY>()) {}

// Note that |input| does not include the first byte of the connection ID.
bool LoadBalancerConfig::InitializeFourPass(const uint8_t* input, uint8_t* left,
                                            uint8_t* right,
                                            uint8_t* half_len) const {
  *half_len = plaintext_len() / 2;
  bool is_length_odd;
  if (plaintext_len() % 2 == 1) {
    ++(*half_len);
    is_length_odd = true;
  } else {
    is_length_odd = false;
  }
  memset(left, 0, kLoadBalancerBlockSize);
  memset(right, 0, kLoadBalancerBlockSize);
  // The first byte is the plaintext/ciphertext length, the second byte will be
  // the index of the pass. Half the plaintext or ciphertext follows.
  left[kLoadBalancerBlockSize - 2] = plaintext_len();
  right[kLoadBalancerBlockSize - 2] = plaintext_len();
  // Leave left_[15]], right_[15] as zero. It will be set for each pass.
  memcpy(left, input, *half_len);
  // If is_length_odd, then both left and right will have part of the middle
  // byte. Then that middle byte will be split in half via the bitmask in the
  // next step.
  memcpy(right, input + (plaintext_len() / 2), *half_len);
  if (is_length_odd) {
    left[*half_len - 1] &= 0xf0;
    right[0] &= 0x0f;
  }
  return is_length_odd;
}

void LoadBalancerConfig::EncryptionPass(uint8_t index, uint8_t half_len,
                                        bool is_length_odd, uint8_t* left,
                                        uint8_t* right) const {
  uint8_t ciphertext[kLoadBalancerBlockSize];
  if (index % 2 == 0) {  // Go right to left.
    right[kLoadBalancerBlockSize - 1] = index;
    AES_encrypt(right, ciphertext, &*key_);
    for (int i = 0; i < half_len; ++i) {
      // Skip over the first two bytes, which have the plaintext_len and the
      // index. The CID bits are in [2, half_len - 1].
      left[i] ^= ciphertext[i];
    }
    if (is_length_odd) {
      left[half_len - 1] &= 0xf0;
    }
    return;
  }
  // Go left to right.
  left[kLoadBalancerBlockSize - 1] = index;
  AES_encrypt(left, ciphertext, &*key_);
  for (int i = 0; i < half_len; ++i) {
    right[i] ^= ciphertext[i];
  }
  if (is_length_odd) {
    right[0] &= 0x0f;
  }
}

}  // namespace quic
