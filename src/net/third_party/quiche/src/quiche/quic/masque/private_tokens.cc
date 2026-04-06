// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/private_tokens.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "anonymous_tokens/cpp/crypto/rsa_blinder.h"
#include "anonymous_tokens/cpp/privacy_pass/rsa_bssa_client.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "openssl/base.h"
#include "openssl/bio.h"
#include "openssl/bn.h"
#include "openssl/digest.h"
#include "openssl/pem.h"
#include "openssl/rand.h"
#include "openssl/rsa.h"
#include "openssl/sha.h"
#include "openssl/sha2.h"
#include "quiche/common/quiche_status_utils.h"

namespace quic {

namespace AT = ::anonymous_tokens;

namespace {

absl::StatusOr<std::unique_ptr<AT::RsaBlinder>> CreateBlinder(
    const RSA* public_key) {
  const BIGNUM* rsa_modulus_bignum = RSA_get0_n(public_key);
  if (rsa_modulus_bignum == nullptr) {
    return absl::InternalError("Failed to get RSA modulus");
  }
  QUICHE_ASSIGN_OR_RETURN(std::string rsa_modulus,
                          AT::BignumToString(*rsa_modulus_bignum,
                                             BN_num_bytes(rsa_modulus_bignum)));
  const BIGNUM* rsa_public_exponent_bignum = RSA_get0_e(public_key);
  if (rsa_public_exponent_bignum == nullptr) {
    return absl::InternalError("Failed to get RSA public exponent");
  }
  QUICHE_ASSIGN_OR_RETURN(
      std::string rsa_public_exponent,
      AT::BignumToString(*rsa_public_exponent_bignum,
                         BN_num_bytes(rsa_public_exponent_bignum)));

  return AT::RsaBlinder::New(rsa_modulus, rsa_public_exponent, EVP_sha384(),
                             EVP_sha384(), SHA384_DIGEST_LENGTH,
                             /*use_rsa_public_exponent=*/false);
}

absl::StatusOr<std::string> BlindSign(RSA* private_key,
                                      absl::string_view blinded_message) {
  if (blinded_message.size() != RSA_size(private_key)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Blind message size ", blinded_message.size(),
                     " does not match RSA size ", RSA_size(private_key)));
  }
  std::string signature(blinded_message.size(), 0);
  // Compute a raw RSA signature.
  size_t out_len;
  if (RSA_sign_raw(
          /*rsa=*/private_key, /*out_len=*/&out_len,
          /*out=*/reinterpret_cast<uint8_t*>(signature.data()),
          /*max_out=*/signature.size(),
          /*in=*/reinterpret_cast<const uint8_t*>(blinded_message.data()),
          /*in_len=*/blinded_message.size(),
          /*padding=*/RSA_NO_PADDING) != 1) {
    return absl::InternalError(
        "RSA_sign_raw failed when called from RsaBlindSigner::Sign");
  }
  if (out_len != signature.size()) {
    return absl::InternalError(absl::StrCat("RSA_sign_raw set out_len to ",
                                            out_len, " instead of ",
                                            signature.size()));
  }
  return signature;
}

}  // namespace

std::string Base64UrlEncodeWithPadding(absl::string_view input) {
  // Private tokens require padded base64url so we need to use the non-URL-safe
  // base64 encoding to get the padding then replace '+' and '/'.
  std::string base64_encoded = absl::Base64Escape(input);
  absl::StrReplaceAll({{"+", "-"}, {"/", "_"}}, &base64_encoded);
  return base64_encoded;
}

absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPrivateKeyFile(
    absl::string_view file_path) {
  BIO* bio = BIO_new_file(file_path.data(), "r");
  if (!bio) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to open file \"", file_path, "\""));
  }
  RSA* rsa_key = PEM_read_bio_RSAPrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!rsa_key) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to read RSA private key from file \"", file_path, "\""));
  }
  return bssl::UniquePtr<RSA>(rsa_key);
}

absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPrivateKeyData(
    absl::string_view pem_data) {
  BIO* bio = BIO_new_mem_buf(pem_data.data(), pem_data.size());
  if (!bio) {
    return absl::InvalidArgumentError("Failed to create BIO from PEM data");
  }
  RSA* rsa_key = PEM_read_bio_RSAPrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!rsa_key) {
    return absl::InvalidArgumentError(
        "Failed to read RSA private key from PEM data");
  }
  return bssl::UniquePtr<RSA>(rsa_key);
}

absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPublicKeyFile(
    absl::string_view file_path) {
  BIO* bio = BIO_new_file(file_path.data(), "r");
  if (!bio) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to open file \"", file_path, "\""));
  }
  RSA* rsa_key = PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!rsa_key) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to read RSA public key from file \"", file_path, "\""));
  }
  return bssl::UniquePtr<RSA>(rsa_key);
}

absl::StatusOr<bssl::UniquePtr<RSA>> ParseRsaPublicKeyData(
    absl::string_view pem_data) {
  BIO* bio = BIO_new_mem_buf(pem_data.data(), pem_data.size());
  if (!bio) {
    return absl::InvalidArgumentError("Failed to create BIO from PEM data");
  }
  RSA* rsa_key = PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!rsa_key) {
    return absl::InvalidArgumentError(
        "Failed to read RSA public key from PEM data");
  }
  return bssl::UniquePtr<RSA>(rsa_key);
}

absl::StatusOr<std::string> EncodePrivacyPassPublicKey(const RSA* public_key) {
  QUICHE_ASSIGN_OR_RETURN(std::string der_encoding,
                          AT::RsaSsaPssPublicKeyToDerEncoding(public_key));
  return Base64UrlEncodeWithPadding(der_encoding);
}

absl::StatusOr<std::string> CreateTokenLocally(RSA* private_key,
                                               const RSA* public_key) {
  QUICHE_ASSIGN_OR_RETURN(std::string public_key_der,
                          AT::RsaSsaPssPublicKeyToDerEncoding(public_key));
  static constexpr size_t kNonceSize = 32;
  static constexpr uint16_t kPrivacyPassBlindRsa2048TokenType = 0x0002;
  static constexpr absl::string_view kChallenge = "";
  AT::Token token;
  token.token_type = kPrivacyPassBlindRsa2048TokenType;
  token.token_key_id = std::string(SHA256_DIGEST_LENGTH, '\0');
  if (SHA256(reinterpret_cast<const uint8_t*>(public_key_der.data()),
             public_key_der.size(),
             reinterpret_cast<uint8_t*>(token.token_key_id.data())) ==
      nullptr) {
    return absl::InternalError("Failed to compute token_key_id");
  }
  token.context = std::string(SHA256_DIGEST_LENGTH, '\0');
  if (SHA256(reinterpret_cast<const uint8_t*>(kChallenge.data()),
             kChallenge.size(),
             reinterpret_cast<uint8_t*>(token.context.data())) == nullptr) {
    return absl::InternalError("Failed to compute context/challenge_digest");
  }
  token.nonce = std::string(kNonceSize, '\0');
  RAND_bytes(reinterpret_cast<uint8_t*>(token.nonce.data()),
             token.nonce.size());
  QUICHE_ASSIGN_OR_RETURN(std::string token_input,
                          AT::AuthenticatorInput(token));
  QUICHE_ASSIGN_OR_RETURN(std::unique_ptr<AT::RsaBlinder> blinder,
                          CreateBlinder(public_key));
  QUICHE_ASSIGN_OR_RETURN(std::string blinded_message,
                          blinder->Blind(token_input));
  QUICHE_ASSIGN_OR_RETURN(std::string blinded_signature,
                          BlindSign(private_key, blinded_message));
  QUICHE_ASSIGN_OR_RETURN(token.authenticator,
                          blinder->Unblind(blinded_signature));
  QUICHE_RETURN_IF_ERROR(blinder->Verify(token.authenticator, token_input));
  QUICHE_ASSIGN_OR_RETURN(std::string token_bytes, AT::MarshalToken(token));
  return Base64UrlEncodeWithPadding(token_bytes);
}

absl::Status ValidateToken(absl::string_view base64_public_key,
                           absl::string_view base64_token) {
  std::string der_public_key;
  if (!absl::WebSafeBase64Unescape(base64_public_key, &der_public_key)) {
    return absl::InvalidArgumentError("Failed to decode the base64 public key");
  }
  std::string token_key_id(SHA256_DIGEST_LENGTH, '\0');
  if (SHA256(reinterpret_cast<const uint8_t*>(der_public_key.data()),
             der_public_key.size(),
             reinterpret_cast<uint8_t*>(token_key_id.data())) == nullptr) {
    return absl::InternalError("Failed to compute token_key_id");
  }
  QUICHE_ASSIGN_OR_RETURN(
      bssl::UniquePtr<RSA> public_key,
      AT::RsaSsaPssPublicKeyFromDerEncoding(der_public_key));
  std::string binary_token;
  if (!absl::WebSafeBase64Unescape(base64_token, &binary_token)) {
    return absl::InvalidArgumentError("Failed to decode the base64 token");
  }

  QUICHE_ASSIGN_OR_RETURN(AT::Token token, AT::UnmarshalToken(binary_token));
  if (token.token_key_id != token_key_id) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Token key ID ", absl::BytesToHexString(token.token_key_id),
        " does not match the public key ID ",
        absl::BytesToHexString(token_key_id)));
  }
  return AT::PrivacyPassRsaBssaClient::Verify(token, *public_key);
}

absl::Status TokenValidatesFromAtLeastOneKey(
    const std::vector<std::string>& base64_public_keys,
    absl::string_view base64_token) {
  absl::Status last_error =
      absl::InvalidArgumentError("No public keys provided");
  for (const std::string& base64_public_key : base64_public_keys) {
    absl::Status status = ValidateToken(base64_public_key, base64_token);
    if (status.ok()) {
      return absl::OkStatus();
    }
    last_error = status;
  }
  return last_error;
}

}  // namespace quic
