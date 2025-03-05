// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_STATUS_UTILS_H_
#define QUICHE_COMMON_QUICHE_STATUS_UTILS_H_

#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

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

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_STATUS_UTILS_H_
