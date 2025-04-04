// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CHACHA_BASE_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_CHACHA_BASE_ENCRYPTER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/aead_base_encrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QUICHE_EXPORT ChaChaBaseEncrypter : public AeadBaseEncrypter {
 public:
  using AeadBaseEncrypter::AeadBaseEncrypter;

  bool SetHeaderProtectionKey(absl::string_view key) override;
  std::string GenerateHeaderProtectionMask(absl::string_view sample) override;

 private:
  // The key used for packet number encryption.
  unsigned char pne_key_[kMaxKeySize];
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CHACHA_BASE_ENCRYPTER_H_
