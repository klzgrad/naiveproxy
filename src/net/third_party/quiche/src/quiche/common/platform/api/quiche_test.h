// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_H_

#include "quiche_platform_impl/quiche_test_impl.h"

namespace quiche::test {

using QuicheTest = QuicheTestImpl;

template <class T>
using QuicheTestWithParam = QuicheTestWithParamImpl<T>;

using QuicheFlagSaver = QuicheFlagSaverImpl;

// Class which needs to be instantiated in tests which use threads.
using ScopedEnvironmentForThreads = ScopedEnvironmentForThreadsImpl;

inline std::string QuicheGetTestMemoryCachePath() {
  return QuicheGetTestMemoryCachePathImpl();
}

// Returns the path to quiche/common directory where the test data could be
// located.
inline std::string QuicheGetCommonSourcePath() {
  return QuicheGetCommonSourcePathImpl();
}

}  // namespace quiche::test

#define EXPECT_QUICHE_DEBUG_DEATH(condition, message) \
  EXPECT_QUICHE_DEBUG_DEATH_IMPL(condition, message)

#define QUICHE_TEST_DISABLED_IN_CHROME(name) \
  QUICHE_TEST_DISABLED_IN_CHROME_IMPL(name)

#define QUICHE_SLOW_TEST(test) QUICHE_SLOW_TEST_IMPL(test)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_H_
