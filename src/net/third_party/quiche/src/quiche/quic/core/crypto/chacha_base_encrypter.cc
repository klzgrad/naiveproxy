// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/chacha_base_encrypter.h"

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "openssl/chacha.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/quiche_endian.h"

namespace quic {

bool ChaChaBaseEncrypter::SetHeaderProtectionKey(absl::string_view key) {
  if (key.size() != GetKeySize()) {
    QUIC_BUG(quic_bug_10656_1) << "Invalid key size for header protection";
    return false;
  }
  memcpy(pne_key_, key.data(), key.size());
  return true;
}

std::string ChaChaBaseEncrypter::GenerateHeaderProtectionMask(
    absl::string_view sample) {
  if (sample.size() != 16) {
    return std::string();
  }
  const uint8_t* nonce = reinterpret_cast<const uint8_t*>(sample.data()) + 4;
  uint32_t counter;
  QuicDataReader(sample.data(), 4, quiche::HOST_BYTE_ORDER)
      .ReadUInt32(&counter);
  const uint8_t zeroes[] = {0, 0, 0, 0, 0};
  std::string out(ABSL_ARRAYSIZE(zeroes), 0);
  CRYPTO_chacha_20(reinterpret_cast<uint8_t*>(const_cast<char*>(out.data())),
                   zeroes, ABSL_ARRAYSIZE(zeroes), pne_key_, nonce, counter);
  return out;
}

}  // namespace quic
