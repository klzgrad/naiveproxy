// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_BORING_UTILS_H_
#define QUICHE_QUIC_CORE_CRYPTO_BORING_UTILS_H_

#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

inline QUIC_EXPORT_PRIVATE quiche::QuicheStringPiece CbsToStringPiece(CBS cbs) {
  return quiche::QuicheStringPiece(
      reinterpret_cast<const char*>(CBS_data(&cbs)), CBS_len(&cbs));
}

inline QUIC_EXPORT_PRIVATE CBS
StringPieceToCbs(quiche::QuicheStringPiece piece) {
  CBS result;
  CBS_init(&result, reinterpret_cast<const uint8_t*>(piece.data()),
           piece.size());
  return result;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_BORING_UTILS_H_
