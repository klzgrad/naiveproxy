// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_H_

#include "net/quiche/common/platform/impl/quiche_test_impl.h"

using QuicheTest = quiche::test::QuicheTest;

template <class T>
using QuicheTestWithParam = quiche::test::QuicheTestWithParamImpl<T>;

namespace quiche {
namespace test {

// Returns the path to quiche/common directory where the test data could be
// located.
inline std::string QuicheGetCommonSourcePath() {
  return QuicheGetCommonSourcePathImpl();
}

}  // namespace test
}  // namespace quiche

#define EXPECT_QUICHE_DEBUG_DEATH(condition, message) \
  EXPECT_QUICHE_DEBUG_DEATH_IMPL(condition, message)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_H_
