// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_

#include <cstddef>
#include <cstdint>

#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QuicDataReader;

// A NullDecrypter is a QuicDecrypter used before a crypto negotiation
// has occurred.  It does not actually decrypt the payload, but does
// verify a hash (fnv128) over both the payload and associated data.
class QUICHE_EXPORT NullDecrypter : public QuicDecrypter {
 public:
  explicit NullDecrypter(Perspective perspective);
  NullDecrypter(const NullDecrypter&) = delete;
  NullDecrypter& operator=(const NullDecrypter&) = delete;
  ~NullDecrypter() override {}

  // QuicDecrypter implementation
  bool SetKey(absl::string_view key) override;
  bool SetNoncePrefix(absl::string_view nonce_prefix) override;
  bool SetIV(absl::string_view iv) override;
  bool SetHeaderProtectionKey(absl::string_view key) override;
  bool SetPreliminaryKey(absl::string_view key) override;
  bool SetDiversificationNonce(const DiversificationNonce& nonce) override;
  bool DecryptPacket(uint64_t packet_number, absl::string_view associated_data,
                     absl::string_view ciphertext, char* output,
                     size_t* output_length, size_t max_output_length) override;
  std::string GenerateHeaderProtectionMask(
      QuicDataReader* sample_reader) override;
  size_t GetKeySize() const override;
  size_t GetNoncePrefixSize() const override;
  size_t GetIVSize() const override;
  absl::string_view GetKey() const override;
  absl::string_view GetNoncePrefix() const override;

  uint32_t cipher_id() const override;
  QuicPacketCount GetIntegrityLimit() const override;

 private:
  bool ReadHash(QuicDataReader* reader, absl::uint128* hash);
  absl::uint128 ComputeHash(absl::string_view data1,
                            absl::string_view data2) const;

  Perspective perspective_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_NULL_DECRYPTER_H_
