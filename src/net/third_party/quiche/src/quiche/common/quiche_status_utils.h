// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_STATUS_UTILS_H_
#define QUICHE_COMMON_QUICHE_STATUS_UTILS_H_

#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"  // IWYU pragma: keep
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace quiche {

// A simplified version of the standard google3 "return if error" macro. Unlike
// the standard version, this does not come with a StatusBuilder support; the
// AppendToStatus() function below is meant to partially fill that gap.
#define QUICHE_RETURN_IF_ERROR(expr)                           \
  do {                                                         \
    absl::Status quiche_status_macro_value = (expr);           \
    if (ABSL_PREDICT_FALSE(!quiche_status_macro_value.ok())) { \
      return quiche_status_macro_value;                        \
    }                                                          \
  } while (0)

// A simplified version of the standard google3 "assign or return" macro.
// Unlike the standard version, this does not come with a StatusBuilder support;
// the optional error-mapper lambda parameter is meant to partially fill that
// gap.
//
// Example usage, where `Calculate()` returns `absl::StatusOr<Foo>`:
//
//   QUICHE_ASSIGN_OR_RETURN(Foo x, Calculate());
//
// Optionally, the macro also accepts an error-mapper lambda that takes a `const
// absl::Status&` and returns `absl::Status`. The lambda is only invoked when
// the status is an error. When it is called, its return value becomes the
// caller's return value.
//
// Example of using the error-mapper lambda to transform the error:
//
//   QUICHE_ASSIGN_OR_RETURN(Foo x, Calculate(),
//                           [](const absl::Status& status) {
//                             return absl::InvalidArgumentError(absl::StrCat(
//                                 "custom message: ", status.message()));
//                           });
//
// Example of using the error-mapper lambda purely for its side effects:
//
//   QUICHE_ASSIGN_OR_RETURN(Foo x, Calculate(),
//                           [](const absl::Status& status) {
//                             QUICHE_LOG(INFO) << status;
//                             return status;
//                           });
#define QUICHE_ASSIGN_OR_RETURN(lhs, expr, ...)                            \
  QUICHE_ASSIGN_OR_RETURN_IMPL(                                            \
      lhs, (expr),                                                         \
      QUICHE_STATUS_UTILS_INTERNAL_CONCAT2(                                \
          quiche_status_utils_internal_statusor, __COUNTER__),             \
      QUICHE_STATUS_UTILS_INTERNAL_CONCAT2(                                \
          quiche_status_utils_internal_status, __COUNTER__),               \
      QUICHE_STATUS_UTILS_INTERNAL_CONCAT2(                                \
          quiche_status_utils_internal_lambda, __COUNTER__) __VA_OPT__(, ) \
          __VA_ARGS__)

// Copies absl::Status payloads from `original` to `target`; required to copy a
// status correctly.
inline void CopyStatusPayloads(const absl::Status& original,
                               absl::Status& target) {
  original.ForEachPayload([&](absl::string_view key, const absl::Cord& value) {
    target.SetPayload(key, value);
  });
}

// Appends additional into to a status message if the status message is
// an error.
template <typename... T>
absl::Status AppendToStatus(absl::Status input, T&&... args) {
  if (ABSL_PREDICT_TRUE(input.ok())) {
    return input;
  }
  absl::Status result = absl::Status(
      input.code(), absl::StrCat(input.message(), std::forward<T>(args)...));
  CopyStatusPayloads(input, result);
  return result;
}

// ========================================================================
// == Implementation details. Do not depend on anything below this line. ==
// ========================================================================

#ifndef __COUNTER__
static_assert(false, "QUICHE_ASSIGN_OR_RETURN requires the __COUNTER__ macro.");
#endif

// clang-format off
#define QUICHE_ASSIGN_OR_RETURN_IMPL(lhs, expr, statusor_ident, status_ident, \
                                     lambda_ident, ...)                       \
  auto statusor_ident = (expr);                                               \
  if (ABSL_PREDICT_FALSE(!statusor_ident.ok())) {                             \
    absl::Status status_ident = statusor_ident.status();                      \
    /* Invoke the error-mapper lambda if the variadic args are non-empty. */  \
    __VA_OPT__(                                                               \
      auto lambda_ident = (__VA_ARGS__);                                      \
      static_assert(std::is_convertible_v<                                    \
                      decltype(lambda_ident),                                 \
                      std::function<absl::Status(const absl::Status&)>>,      \
                   "QUICHE_ASSIGN_OR_RETURN: the on-error lambda has an "     \
                   "unexpected type");                                        \
      status_ident = lambda_ident(status_ident);                              \
    )                                                                         \
    return status_ident;                                                      \
  }                                                                           \
  lhs = std::move(statusor_ident).value();
// clang-format on

#define QUICHE_STATUS_UTILS_INTERNAL_CONCAT2(a, b) \
  QUICHE_STATUS_UTILS_INTERNAL_CONCAT2_IMPL(a, b)
// Without this multi-layer concat macro, `__COUNTER__` would not be expanded.
#define QUICHE_STATUS_UTILS_INTERNAL_CONCAT2_IMPL(a, b) a##b

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_STATUS_UTILS_H_
