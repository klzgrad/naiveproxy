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

#ifndef THIRD_PARTY_ANONYMOUS_TOKENS_CPP_SHARED_STATUS_UTILS_H_
#define THIRD_PARTY_ANONYMOUS_TOKENS_CPP_SHARED_STATUS_UTILS_H_

#include "absl/base/optimization.h"
#include "absl/status/status.h"

namespace private_membership {
namespace anonymous_tokens {

#define _ANON_TOKENS_STATUS_MACROS_CONCAT_NAME(x, y) \
  _ANON_TOKENS_STATUS_MACROS_CONCAT_IMPL(x, y)
#define _ANON_TOKENS_STATUS_MACROS_CONCAT_IMPL(x, y) x##y

#define ANON_TOKENS_ASSIGN_OR_RETURN(lhs, rexpr)                             \
  _ANON_TOKENS_ASSIGN_OR_RETURN_IMPL(                                        \
      _ANON_TOKENS_STATUS_MACROS_CONCAT_NAME(_status_or_val, __LINE__), lhs, \
      rexpr)

#define _ANON_TOKENS_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                       \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {                      \
    return statusor.status();                                    \
  }                                                              \
  lhs = *std::move(statusor)

#define ANON_TOKENS_RETURN_IF_ERROR(expr)                  \
  do {                                                     \
    auto _status = (expr);                                 \
    if (ABSL_PREDICT_FALSE(!_status.ok())) return _status; \
  } while (0)

}  // namespace anonymous_tokens
}  // namespace private_membership

#endif  // THIRD_PARTY_ANONYMOUS_TOKENS_CPP_SHARED_STATUS_UTILS_H_
