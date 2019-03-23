// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_HKDF_H_
#define CRYPTO_HKDF_H_

#include <stddef.h>

#include <string>

#include "base/strings/string_piece.h"
#include "crypto/crypto_export.h"

namespace crypto {

CRYPTO_EXPORT
std::string HkdfSha256(base::StringPiece secret,
                       base::StringPiece salt,
                       base::StringPiece info,
                       size_t derived_key_size);

}  // namespace crypto

#endif  // CRYPTO_HKDF_H_
