// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/blind_sign_auth/blind_sign_auth_test_data.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "anonymous_tokens/cpp/crypto/crypto_utils.h"
#include "anonymous_tokens/cpp/privacy_pass/token_encodings.h"
#include "anonymous_tokens/cpp/testing/utils.h"
#include "openssl/base.h"
#include "openssl/digest.h"

namespace quiche::test {

using ::privacy::ppn::AttestAndSignResponse;
using ::privacy::ppn::GetInitialDataResponse;
using ::anonymous_tokens::AT_HASH_TYPE_SHA384;
using ::anonymous_tokens::AT_MESSAGE_MASK_NO_MASK;
using ::anonymous_tokens::AT_MGF_SHA384;
using ::anonymous_tokens::ComputeHash;
using ::anonymous_tokens::CreatePrivateKeyRSA;
using ::anonymous_tokens::CreatePublicKeyRSA;
using ::anonymous_tokens::DebugMode;
using ::anonymous_tokens::EncodeExtensions;
using ::anonymous_tokens::ExpirationTimestamp;
using ::anonymous_tokens::Extension;
using ::anonymous_tokens::Extensions;
using ::anonymous_tokens::GeoHint;
using ::anonymous_tokens::GetStrongTestRsaKeyPair2048;
using ::anonymous_tokens::ProxyLayer;
using ::anonymous_tokens::RSABlindSignaturePublicKey;
using ::anonymous_tokens::RSAPublicKey;
using ::anonymous_tokens::RsaSsaPssPublicKeyToDerEncoding;
using ::anonymous_tokens::ServiceType;
using ::anonymous_tokens::TestRsaPublicKey;

namespace {

// Helpers
absl::StatusOr<Extensions> CreateExtensions();
RSABlindSignaturePublicKey CreatePublicKeyProto(
    TestRsaPublicKey test_rsa_public_key);

}  // namespace

absl::StatusOr<std::unique_ptr<BlindSignAuthTestData>>
BlindSignAuthTestData::Create() {
  // Create keypair and populate protos.
  auto [test_rsa_public_key, test_rsa_private_key] =
      GetStrongTestRsaKeyPair2048();
  absl::StatusOr<bssl::UniquePtr<RSA>> rsa_public_key =
      CreatePublicKeyRSA(test_rsa_public_key.n, test_rsa_public_key.e);
  if (!rsa_public_key.ok()) {
    return rsa_public_key.status();
  }

  absl::StatusOr<bssl::UniquePtr<RSA>> rsa_private_key = CreatePrivateKeyRSA(
      test_rsa_private_key.n, test_rsa_private_key.e, test_rsa_private_key.d,
      test_rsa_private_key.p, test_rsa_private_key.q, test_rsa_private_key.dp,
      test_rsa_private_key.dq, test_rsa_private_key.crt);
  if (!rsa_private_key.ok()) {
    return rsa_private_key.status();
  }

  // token_key_id is derived from public key.
  absl::StatusOr<std::string> public_key_der =
      RsaSsaPssPublicKeyToDerEncoding(rsa_public_key->get());
  if (!public_key_der.ok()) {
    return public_key_der.status();
  }

  const EVP_MD *sha256 = EVP_sha256();
  absl::StatusOr<std::string> token_key_id =
      ComputeHash(*public_key_der, *sha256);
  if (!token_key_id.ok()) {
    return token_key_id.status();
  }

  absl::StatusOr<Extensions> extensions = CreateExtensions();
  if (!extensions.ok()) {
    return extensions.status();
  }

  absl::StatusOr<std::string> serialized_extensions =
      EncodeExtensions(*extensions);
  if (!serialized_extensions.ok()) {
    return serialized_extensions.status();
  }

  GetInitialDataResponse::PrivacyPassData privacy_pass_data;
  privacy_pass_data.set_token_key_id(*token_key_id);
  privacy_pass_data.set_public_metadata_extensions(*serialized_extensions);

  return absl::WrapUnique(new BlindSignAuthTestData(
      std::move(*rsa_public_key), std::move(*rsa_private_key),
      CreatePublicKeyProto(test_rsa_public_key), privacy_pass_data));
}

GetInitialDataResponse BlindSignAuthTestData::CreateGetInitialDataResponse() {
  GetInitialDataResponse response;

  *response.mutable_at_public_metadata_public_key() = public_key_proto_;
  *response.mutable_privacy_pass_data() = privacy_pass_data_;

  // Create fake GetInitialDataResponse for attestation flow.
  response.mutable_attestation()->set_attestation_nonce(
      "test_attestation_nonce");
  return response;
}

absl::StatusOr<privacy::ppn::AttestAndSignResponse>
BlindSignAuthTestData::CreateAttestAndSignResponse(absl::string_view body) {
  privacy::ppn::AttestAndSignRequest request;
  if (!request.ParseFromString(body)) {
    return absl::InvalidArgumentError("Failed to parse AttestAndSignRequest");
  }

  privacy::ppn::AttestAndSignResponse response;
  for (const auto &request_token : request.blinded_tokens()) {
    std::string decoded_blinded_token;
    if (!absl::Base64Unescape(request_token, &decoded_blinded_token)) {
      return absl::InvalidArgumentError("Failed to decode blinded token");
    }
    absl::StatusOr<std::string> signature =
        anonymous_tokens::TestSignWithPublicMetadata(
            decoded_blinded_token,
            privacy_pass_data_.public_metadata_extensions(), *rsa_private_key_,
            false);
    if (!signature.ok()) {
      return signature.status();
    }
    response.add_blinded_token_signatures(absl::Base64Escape(*signature));
  }

  return response;
}

namespace {

absl::StatusOr<anonymous_tokens::Extensions>
CreateExtensions() {
  // Create and serialize fake extensions.
  Extensions extensions;
  ExpirationTimestamp expiration_timestamp;
  int64_t one_hour_away = absl::ToUnixSeconds(absl::Now() + absl::Hours(1));
  expiration_timestamp.timestamp = one_hour_away - (one_hour_away % 900);
  expiration_timestamp.timestamp_precision = 900;
  absl::StatusOr<anonymous_tokens::Extension>
      expiration_extension = expiration_timestamp.AsExtension();
  if (!expiration_extension.ok()) {
    return expiration_extension.status();
  }
  extensions.extensions.push_back(*expiration_extension);

  GeoHint geo_hint;
  geo_hint.geo_hint = "US,US-AL,ALABASTER";
  absl::StatusOr<Extension> geo_hint_extension = geo_hint.AsExtension();
  if (!geo_hint_extension.ok()) {
    return geo_hint_extension.status();
  }
  extensions.extensions.push_back(*geo_hint_extension);

  ServiceType service_type;
  service_type.service_type_id = ServiceType::kChromeIpBlinding;
  absl::StatusOr<Extension> service_type_extension = service_type.AsExtension();
  if (!service_type_extension.ok()) {
    return service_type_extension.status();
  }
  extensions.extensions.push_back(*service_type_extension);

  DebugMode debug_mode;
  debug_mode.mode = DebugMode::kDebug;
  absl::StatusOr<Extension> debug_mode_extension = debug_mode.AsExtension();
  if (!debug_mode_extension.ok()) {
    return debug_mode_extension.status();
  }
  extensions.extensions.push_back(*debug_mode_extension);

  ProxyLayer proxy_layer;
  proxy_layer.layer = ProxyLayer::kProxyA;
  absl::StatusOr<Extension> proxy_layer_extension = proxy_layer.AsExtension();
  if (!proxy_layer_extension.ok()) {
    return proxy_layer_extension.status();
  }
  extensions.extensions.push_back(*proxy_layer_extension);

  return extensions;
}

RSABlindSignaturePublicKey CreatePublicKeyProto(
    TestRsaPublicKey test_rsa_public_key) {
  RSAPublicKey public_key;
  public_key.set_n(test_rsa_public_key.n);
  public_key.set_e(test_rsa_public_key.e);
  RSABlindSignaturePublicKey public_key_proto;
  public_key_proto.set_key_version(1);
  public_key_proto.set_use_case("TEST_USE_CASE");
  public_key_proto.set_serialized_public_key(public_key.SerializeAsString());
  public_key_proto.set_sig_hash_type(AT_HASH_TYPE_SHA384);
  public_key_proto.set_mask_gen_function(AT_MGF_SHA384);
  public_key_proto.set_salt_length(48);
  public_key_proto.set_key_size(256);
  public_key_proto.set_message_mask_type(AT_MESSAGE_MASK_NO_MASK);
  public_key_proto.set_message_mask_size(0);

  return public_key_proto;
}

}  // namespace

}  // namespace quiche::test
