// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_TEST_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_TEST_IMPL_H_

#include "base/logging.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"  // IWYU pragma: export
#include "testing/gtest/include/gtest/gtest.h"  // IWYU pragma: export

// When constructed, saves the current values of all QUIC flags. When
// destructed, restores all QUIC flags to the saved values.
class QuicFlagSaverImpl {
 public:
  QuicFlagSaverImpl();
  ~QuicFlagSaverImpl();

 private:
#define QUIC_FLAG(type, flag, value) type saved_##flag##_;
#include "net/quic/quic_flags_list.h"
#undef QUIC_FLAG
};

// Checks if all QUIC flags are on their default values on construction.
class QuicFlagChecker {
 public:
  QuicFlagChecker() {
#define QUIC_FLAG(type, flag, value)                                      \
  CHECK_EQ(value, flag)                                                   \
      << "Flag set to an unexpected value.  A prior test is likely "      \
      << "setting a flag without using a QuicFlagSaver. Use QuicTest to " \
         "avoid this issue.";
#include "net/quic/quic_flags_list.h"
#undef QUIC_FLAG
  }
};

class QuicTestImpl : public ::testing::Test {
 private:
  QuicFlagChecker checker_;
  QuicFlagSaverImpl saver_;  // Save/restore all QUIC flag values.
};

template <class T>
class QuicTestWithParamImpl : public ::testing::TestWithParam<T> {
 private:
  QuicFlagChecker checker_;
  QuicFlagSaverImpl saver_;  // Save/restore all QUIC flag values.
};

class ScopedEnvironmentForThreadsImpl {
 public:
  ScopedEnvironmentForThreadsImpl()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::IO) {}

 public:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
};

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_TEST_IMPL_H_
