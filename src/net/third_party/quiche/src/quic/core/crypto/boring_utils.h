// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_BORING_UTILS_H_
#define QUICHE_QUIC_CORE_CRYPTO_BORING_UTILS_H_

#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

inline QUIC_EXPORT_PRIVATE absl::string_view CbsToStringPiece(CBS cbs) {
  return absl::string_view(reinterpret_cast<const char*>(CBS_data(&cbs)),
                           CBS_len(&cbs));
}

inline QUIC_EXPORT_PRIVATE CBS StringPieceToCbs(absl::string_view piece) {
  CBS result;
  CBS_init(&result, reinterpret_cast<const uint8_t*>(piece.data()),
           piece.size());
  return result;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_BORING_UTILS_H_
