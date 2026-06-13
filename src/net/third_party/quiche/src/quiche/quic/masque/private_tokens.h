// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_PRIVATE_TOKENS_H_
#define QUICHE_QUIC_MASQUE_PRIVATE_TOKENS_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/rsa.h"

namespace quic {

// PRIVACYPASS requires base64url but with padding.
std::string Base64UrlEncodeWithPadding(absl::string_view input);

// Parse an RSA private key from the given file path in PEM format.
absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPrivateKeyFile(
    absl::string_view file_path);

// Parse an RSA private key from the given string in PEM format.
absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPrivateKeyData(
    absl::string_view pem_data);

// Parse an RSA public key from the given file path in PEM format.
absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPublicKeyFile(
    absl::string_view file_path);

// Parse an RSA public key from the given string in PEM format.
absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPublicKeyData(
    absl::string_view pem_data);

// Encodes the key into a entry in the base64 token-key object from the
// PRIVACYPASS RFC. https://www.rfc-editor.org/rfc/rfc9578.html#section-4
absl::StatusOr<std::string> EncodePrivacyPassPublicKey(const RSA* public_key);

// Performs both the client and issuer sides of the blind signature protocol
// locally.
absl::StatusOr<std::string> CreateTokenLocally(RSA* private_key,
                                               const RSA* public_key);

// Checks that a token is valid for the given public key. Takes the token and
// public key as base64 encoded strings in the format from RFC 9578.
absl::Status ValidateToken(absl::string_view base64_public_key,
                           absl::string_view base64_token);

// Checks the token against all keys using ValidateToken above.
absl::Status TokenValidatesFromAtLeastOneKey(
    const std::vector<std::string>& base64_public_keys,
    absl::string_view base64_token);

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_PRIVATE_TOKENS_H_
