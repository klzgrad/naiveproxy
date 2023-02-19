// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_TEST_TOOLS_QUICHE_TEST_UTILS_H_
#define QUICHE_COMMON_TEST_TOOLS_QUICHE_TEST_UTILS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_iovec.h"

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

}  // namespace test
}  // namespace quiche

#endif  // QUICHE_COMMON_TEST_TOOLS_QUICHE_TEST_UTILS_H_
