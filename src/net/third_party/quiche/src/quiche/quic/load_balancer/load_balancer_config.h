// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_CONFIG_H_
#define QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_CONFIG_H_

#include "openssl/aes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

inline constexpr uint8_t kNumLoadBalancerConfigs = 3;
inline constexpr uint8_t kLoadBalancerKeyLen = 16;
// Regardless of key length, the AES block size is always 16 Bytes.
inline constexpr uint8_t kLoadBalancerBlockSize = 16;
// The spec says nonces can be 18 bytes, but 16 lets it be a uint128.
inline constexpr uint8_t kLoadBalancerMaxNonceLen = 16;
inline constexpr uint8_t kLoadBalancerMinNonceLen = 4;
inline constexpr uint8_t kNumLoadBalancerCryptoPasses = 4;

// This the base class for QUIC-LB configuration. It contains configuration
// elements usable by both encoders (servers) and decoders (load balancers).
// Confusingly, it is called "LoadBalancerConfig" because it pertains to objects
// that both servers and load balancers use to interact with each other.
class QUIC_EXPORT_PRIVATE LoadBalancerConfig {
 public:
  // This factory function initializes an encrypted LoadBalancerConfig and
  // returns it in absl::optional, which is empty if the config is invalid.
  // config_id: The first two bits of the Connection Id. Must be no larger than
  // 2.
  // server_id_len: Expected length of the server ids associated with this
  // config. Must be greater than 0 and less than 16.
  // nonce_len: Length of the nonce. Must be at least 4 and no larger than 16.
  // Further the server_id_len + nonce_len must be no larger than 19.
  // key: The encryption key must be 16B long.
  static absl::optional<LoadBalancerConfig> Create(const uint8_t config_id,
                                                   const uint8_t server_id_len,
                                                   const uint8_t nonce_len,
                                                   const absl::string_view key);

  // Creates an unencrypted config.
  static absl::optional<LoadBalancerConfig> CreateUnencrypted(
      const uint8_t config_id, const uint8_t server_id_len,
      const uint8_t nonce_len);

  // Handles one pass of 4-pass encryption. Encoder and decoder use of this
  // function varies substantially, so they are not implemented here.
  // Returns false if the config is not encrypted, or if |target| isn't long
  // enough.
  ABSL_MUST_USE_RESULT bool EncryptionPass(absl::Span<uint8_t> target,
                                           const uint8_t index) const;
  // Use the key to do a block encryption, which is used both in all cases of
  // encrypted configs. Returns false if there's no key.
  ABSL_MUST_USE_RESULT bool BlockEncrypt(
      const uint8_t plaintext[kLoadBalancerBlockSize],
      uint8_t ciphertext[kLoadBalancerBlockSize]) const;
  // Returns false if the config does not require block decryption.
  ABSL_MUST_USE_RESULT bool BlockDecrypt(
      const uint8_t ciphertext[kLoadBalancerBlockSize],
      uint8_t plaintext[kLoadBalancerBlockSize]) const;

  uint8_t config_id() const { return config_id_; }
  uint8_t server_id_len() const { return server_id_len_; }
  uint8_t nonce_len() const { return nonce_len_; }
  // Returns length of all but the first octet.
  uint8_t plaintext_len() const { return server_id_len_ + nonce_len_; }
  // Returns length of the entire connection ID.
  uint8_t total_len() const { return server_id_len_ + nonce_len_ + 1; }
  bool IsEncrypted() const { return key_.has_value(); }

 private:
  // Constructor is private because it doesn't validate input.
  LoadBalancerConfig(uint8_t config_id, uint8_t server_id_len,
                     uint8_t nonce_len, absl::string_view key);

  uint8_t config_id_;
  uint8_t server_id_len_;
  uint8_t nonce_len_;
  // All Connection ID encryption and decryption uses the AES_encrypt function
  // at root, so there is a single key for all of it. This is empty if the
  // config is not encrypted.
  absl::optional<AES_KEY> key_;
  // The one exception is that when total_len == 16, connection ID decryption
  // uses AES_decrypt. The bytes that comprise the key are the same, but
  // AES_decrypt requires an AES_KEY that is initialized differently. In all
  // other cases, block_decrypt_key_ is empty.
  absl::optional<AES_KEY> block_decrypt_key_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_CONFIG_H_
