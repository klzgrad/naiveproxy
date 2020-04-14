// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/chacha_base_encrypter.h"

#include "third_party/boringssl/src/include/openssl/chacha.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

bool ChaChaBaseEncrypter::SetHeaderProtectionKey(
    quiche::QuicheStringPiece key) {
  if (key.size() != GetKeySize()) {
    QUIC_BUG << "Invalid key size for header protection";
    return false;
  }
  memcpy(pne_key_, key.data(), key.size());
  return true;
}

std::string ChaChaBaseEncrypter::GenerateHeaderProtectionMask(
    quiche::QuicheStringPiece sample) {
  if (sample.size() != 16) {
    return std::string();
  }
  const uint8_t* nonce = reinterpret_cast<const uint8_t*>(sample.data()) + 4;
  uint32_t counter;
  QuicDataReader(sample.data(), 4, quiche::HOST_BYTE_ORDER)
      .ReadUInt32(&counter);
  const uint8_t zeroes[] = {0, 0, 0, 0, 0};
  std::string out(QUICHE_ARRAYSIZE(zeroes), 0);
  CRYPTO_chacha_20(reinterpret_cast<uint8_t*>(const_cast<char*>(out.data())),
                   zeroes, QUICHE_ARRAYSIZE(zeroes), pne_key_, nonce, counter);
  return out;
}

}  // namespace quic
