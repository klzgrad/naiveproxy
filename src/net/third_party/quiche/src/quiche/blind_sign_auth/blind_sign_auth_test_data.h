// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_TEST_DATA_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_TEST_DATA_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "anonymous_tokens/cpp/crypto/crypto_utils.h"  // IWYU pragma: keep
#include "openssl/base.h"
#include "quiche/blind_sign_auth/blind_sign_auth_protos.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche::test {

class QUICHE_NO_EXPORT BlindSignAuthTestData {
 public:
  static absl::StatusOr<std::unique_ptr<BlindSignAuthTestData>> Create();

  privacy::ppn::GetInitialDataResponse CreateGetInitialDataResponse();

  absl::StatusOr<privacy::ppn::AttestAndSignResponse>
  CreateAttestAndSignResponse(absl::string_view body);

 private:
  BlindSignAuthTestData(
      bssl::UniquePtr<RSA> rsa_public_key, bssl::UniquePtr<RSA> rsa_private_key,
      anonymous_tokens::RSABlindSignaturePublicKey
          public_key_proto,
      privacy::ppn::GetInitialDataResponse::PrivacyPassData privacy_pass_data)
      : rsa_public_key_(std::move(rsa_public_key)),
        rsa_private_key_(std::move(rsa_private_key)),
        public_key_proto_(std::move(public_key_proto)),
        privacy_pass_data_(std::move(privacy_pass_data)) {}

  bssl::UniquePtr<RSA> rsa_public_key_;
  bssl::UniquePtr<RSA> rsa_private_key_;
  anonymous_tokens::RSABlindSignaturePublicKey
      public_key_proto_;
  privacy::ppn::GetInitialDataResponse::PrivacyPassData privacy_pass_data_;
};

}  // namespace quiche::test

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_AUTH_TEST_DATA_H_
