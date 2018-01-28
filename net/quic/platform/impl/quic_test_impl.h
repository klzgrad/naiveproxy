// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_TEST_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_TEST_IMPL_H_

#include "testing/gmock/include/gmock/gmock.h"  // IWYU pragma: export
#include "testing/gtest/include/gtest/gtest.h"  // IWYU pragma: export

// When constructed, checks that all QUIC flags have their correct default
// values and when destructed, restores those values.
class QuicFlagSaver {
 public:
  QuicFlagSaver();
  ~QuicFlagSaver();
};

class QuicTestImpl : public ::testing::Test {
 private:
  QuicFlagSaver flags_;  // Save/restore all QUIC flag values.
};

template <class T>
class QuicTestWithParamImpl : public ::testing::TestWithParam<T> {
 private:
  QuicFlagSaver flags_;  // Save/restore all QUIC flag values.
};

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_TEST_IMPL_H_
