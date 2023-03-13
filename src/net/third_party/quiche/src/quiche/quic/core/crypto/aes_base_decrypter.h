// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AES_BASE_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AES_BASE_DECRYPTER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "openssl/aes.h"
#include "quiche/quic/core/crypto/aead_base_decrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QUIC_EXPORT_PRIVATE AesBaseDecrypter : public AeadBaseDecrypter {
 public:
  using AeadBaseDecrypter::AeadBaseDecrypter;

  bool SetHeaderProtectionKey(absl::string_view key) override;
  std::string GenerateHeaderProtectionMask(
      QuicDataReader* sample_reader) override;
  QuicPacketCount GetIntegrityLimit() const override;

 private:
  // The key used for packet number encryption.
  AES_KEY pne_key_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AES_BASE_DECRYPTER_H_
