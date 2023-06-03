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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_BLINDER_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_BLINDER_H_

#include <string>

#include "absl/status/statusor.h"

namespace private_membership {
namespace anonymous_tokens {

class Blinder {
 public:
  enum class BlinderState { kCreated = 0, kBlinded, kUnblinded };
  virtual absl::StatusOr<std::string> Blind(absl::string_view message) = 0;

  virtual absl::StatusOr<std::string> Unblind(
      absl::string_view blind_signature) = 0;

  virtual ~Blinder() = default;
};

}  // namespace anonymous_tokens
}  // namespace private_membership
#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_CRYPTO_BLINDER_H_
