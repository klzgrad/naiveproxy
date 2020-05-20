// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_TEST_UTILS_H_
#define QUICHE_COMMON_QUICHE_TEST_UTILS_H_

#include <string>

namespace quiche {
namespace test {

void CompareCharArraysWithHexError(const std::string& description,
                                   const char* actual,
                                   const int actual_len,
                                   const char* expected,
                                   const int expected_len);

}  // namespace test
}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_TEST_UTILS_H_
