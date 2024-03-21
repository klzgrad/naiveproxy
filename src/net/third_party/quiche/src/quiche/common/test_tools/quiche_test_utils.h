// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_TEST_TOOLS_QUICHE_TEST_UTILS_H_
#define QUICHE_COMMON_TEST_TOOLS_QUICHE_TEST_UTILS_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_iovec.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace quiche {
namespace test {

void CompareCharArraysWithHexError(const std::string& description,
                                   const char* actual, const int actual_len,
                                   const char* expected,
                                   const int expected_len);

// Create iovec that points to that data that `str` points to.
iovec MakeIOVector(absl::string_view str);

// Due to binary size considerations, googleurl library can be built with or
// without IDNA support, meaning that we have to adjust our tests accordingly.
// This function checks if IDNAs are supported.
bool GoogleUrlSupportsIdnaForTest();

// Takes either a Status or StatusOr<T>, and returns just the Status.
inline const absl::Status& ExtractStatus(const absl::Status& status) {
  return status;
}
template <typename T>
const absl::Status& ExtractStatus(const absl::StatusOr<T>& status_or) {
  return status_or.status();
}

// Abseil does not provide absl::Status-related macros, so we have to provide
// those instead.
MATCHER(IsOk, "Checks if an instance of absl::Status is ok.") {
  if (arg.ok()) {
    return true;
  }
  *result_listener << "Expected status OK, got " << ExtractStatus(arg);
  return false;
}

MATCHER_P(IsOkAndHolds, matcher,
          "Matcher against the inner value of absl::StatusOr") {
  if (!arg.ok()) {
    *result_listener << "Expected status OK, got " << arg.status();
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, arg.value(), result_listener);
}

MATCHER_P(StatusIs, code, "Matcher against only a specific status code") {
  if (ExtractStatus(arg).code() != code) {
    *result_listener << "Expected status " << absl::StatusCodeToString(code)
                     << ", got " << ExtractStatus(arg);
    return false;
  }
  return true;
}

MATCHER_P2(StatusIs, code, matcher, "Matcher against a specific status code") {
  if (ExtractStatus(arg).code() != code) {
    *result_listener << "Expected status " << absl::StatusCodeToString(code)
                     << ", got " << ExtractStatus(arg);
    return false;
  }
  return ::testing::ExplainMatchResult(matcher, ExtractStatus(arg).message(),
                                       result_listener);
}

#define QUICHE_EXPECT_OK(arg) EXPECT_THAT((arg), ::quiche::test::IsOk())
#define QUICHE_ASSERT_OK(arg) ASSERT_THAT((arg), ::quiche::test::IsOk())

}  // namespace test
}  // namespace quiche

#endif  // QUICHE_COMMON_TEST_TOOLS_QUICHE_TEST_UTILS_H_
