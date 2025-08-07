// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This interface is deprecated and being removed: https://crbug.com/374310081.
// New users should use crypto/hash instead.

#ifndef CRYPTO_SHA2_H_
#define CRYPTO_SHA2_H_

#include <stddef.h>

#include <array>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "crypto/crypto_export.h"

namespace crypto {

// These functions perform SHA-256 operations.
//
// Functions for SHA-384 and SHA-512 can be added when the need arises.
//
// Deprecated: use the interface in crypto/hash.h instead.
// TODO(https://crbug.com/374310081): Delete these.

static const size_t kSHA256Length = 32;  // Length in bytes of a SHA-256 hash.

// Computes the SHA-256 hash of |input|.
CRYPTO_EXPORT std::array<uint8_t, kSHA256Length> SHA256Hash(
    base::span<const uint8_t> input);

// Convenience version of the above that returns the result in a 32-byte
// string.
CRYPTO_EXPORT std::string SHA256HashString(std::string_view str);

}  // namespace crypto

#endif  // CRYPTO_SHA2_H_
