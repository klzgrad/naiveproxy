// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_CONFIG_H_
#define QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_CONFIG_H_

#include <cstdint>
#include <optional>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "openssl/aes.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/load_balancer/load_balancer_server_id.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class LoadBalancerConfigPeer;
}  // namespace test

// The number of bits in the first byte used for the config ID
inline constexpr uint8_t kConfigIdBits = 3;
// The number of bits in the first byte used for the connection ID length, if
// the encoder uses this option. Otherwise, by spec it's random bits.
inline constexpr uint8_t kConnectionIdLengthBits = 8 - kConfigIdBits;
// One codepoint is reserved for unroutable connection IDs, so subtract one to
// find the maximum number of configs.
inline constexpr uint8_t kNumLoadBalancerConfigs = (1 << kConfigIdBits) - 1;
inline constexpr uint8_t kLoadBalancerKeyLen = 16;
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
  // returns it in std::optional, which is empty if the config is invalid.
  // config_id: The first two bits of the Connection Id. Must be no larger than
  // 2.
  // server_id_len: Expected length of the server ids associated with this
  // config. Must be greater than 0 and less than 16.
  // nonce_len: Length of the nonce. Must be at least 4 and no larger than 16.
  // Further the server_id_len + nonce_len must be no larger than 19.
  // key: The encryption key must be 16B long.
  static std::optional<LoadBalancerConfig> Create(uint8_t config_id,
                                                  uint8_t server_id_len,
                                                  uint8_t nonce_len,
                                                  absl::string_view key);

  // Creates an unencrypted config.
  static std::optional<LoadBalancerConfig> CreateUnencrypted(
      uint8_t config_id, uint8_t server_id_len, uint8_t nonce_len);

  // Returns an invalid Server ID if ciphertext is too small, or needed keys are
  // missing. |ciphertext| contains the full connection ID minus the first byte.
  //
  // IMPORTANT: The decoder data path is likely the most performance-sensitive
  // part of the load balancer design, and this code has been carefully
  // optimized for performance. Please do not make changes without running the
  // benchmark tests to ensure there is no regression.
  bool FourPassDecrypt(absl::Span<const uint8_t> ciphertext,
                       LoadBalancerServerId& server_id) const;
  // Returns an empty connection ID if the plaintext is too small, or needed
  // keys are missing. |plaintext| contains the full unencrypted connection ID,
  // including the first byte.
  QuicConnectionId FourPassEncrypt(absl::Span<uint8_t> plaintext) const;

  // Use the key to do a block encryption, which is used both in all cases of
  // encrypted configs. Returns false if there's no key. Type char is
  // convenient because that's what QuicConnectionId uses.
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
  friend class test::LoadBalancerConfigPeer;

  // Constructor is private because it doesn't validate input.
  LoadBalancerConfig(uint8_t config_id, uint8_t server_id_len,
                     uint8_t nonce_len, absl::string_view key);

  // Initialize state for 4-pass encryption passes, using the connection ID
  // provided in |input|. Returns true if the plaintext is an odd number of
  // bytes. |half_len| is half the length of the plaintext, rounded up.
  bool InitializeFourPass(const uint8_t* input, uint8_t* left, uint8_t* right,
                          uint8_t* half_len) const;
  // Handles one pass of 4-pass encryption for both encrypt and decrypt.
  void EncryptionPass(uint8_t index, uint8_t half_len, bool is_length_odd,
                      uint8_t* left, uint8_t* right) const;

  uint8_t config_id_;
  uint8_t server_id_len_;
  uint8_t nonce_len_;
  // All Connection ID encryption and decryption uses the AES_encrypt function
  // at root, so there is a single key for all of it. This is empty if the
  // config is not encrypted.
  std::optional<AES_KEY> key_;
  // The one exception is that when total_len == 16, connection ID decryption
  // uses AES_decrypt. The bytes that comprise the key are the same, but
  // AES_decrypt requires an AES_KEY that is initialized differently. In all
  // other cases, block_decrypt_key_ is empty.
  std::optional<AES_KEY> block_decrypt_key_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_CONFIG_H_
