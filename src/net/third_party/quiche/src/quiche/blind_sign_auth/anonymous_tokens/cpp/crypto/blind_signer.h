// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_BLIND_SIGNER_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_BLIND_SIGNER_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace private_membership {
namespace anonymous_tokens {

class BlindSigner {
 public:
  virtual absl::StatusOr<std::string> Sign(
      absl::string_view blinded_data) const = 0;

  virtual ~BlindSigner() = default;
};

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_BLIND_SIGNER_H_
