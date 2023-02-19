// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_NULL_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_NULL_ENCRYPTER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A NullEncrypter is a QuicEncrypter used before a crypto negotiation
// has occurred.  It does not actually encrypt the payload, but does
// generate a MAC (fnv128) over both the payload and associated data.
class QUIC_EXPORT_PRIVATE NullEncrypter : public QuicEncrypter {
 public:
  explicit NullEncrypter(Perspective perspective);
  NullEncrypter(const NullEncrypter&) = delete;
  NullEncrypter& operator=(const NullEncrypter&) = delete;
  ~NullEncrypter() override {}

  // QuicEncrypter implementation
  bool SetKey(absl::string_view key) override;
  bool SetNoncePrefix(absl::string_view nonce_prefix) override;
  bool SetIV(absl::string_view iv) override;
  bool SetHeaderProtectionKey(absl::string_view key) override;
  bool EncryptPacket(uint64_t packet_number, absl::string_view associated_data,
                     absl::string_view plaintext, char* output,
                     size_t* output_length, size_t max_output_length) override;
  std::string GenerateHeaderProtectionMask(absl::string_view sample) override;
  size_t GetKeySize() const override;
  size_t GetNoncePrefixSize() const override;
  size_t GetIVSize() const override;
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override;
  size_t GetCiphertextSize(size_t plaintext_size) const override;
  QuicPacketCount GetConfidentialityLimit() const override;
  absl::string_view GetKey() const override;
  absl::string_view GetNoncePrefix() const override;

 private:
  size_t GetHashLength() const;

  Perspective perspective_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_NULL_ENCRYPTER_H_
