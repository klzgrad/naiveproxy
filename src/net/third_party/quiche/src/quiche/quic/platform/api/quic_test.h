// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TEST_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TEST_H_

#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace quic::test {

using QuicFlagSaver = quiche::test::QuicheFlagSaver;

// Defines the base classes to be used in QUIC tests.
using QuicTest = quiche::test::QuicheTest;
template <class T>
using QuicTestWithParam = quiche::test::QuicheTestWithParam<T>;

}  // namespace quic::test

#define QUIC_TEST_DISABLED_IN_CHROME(name) QUICHE_TEST_DISABLED_IN_CHROME(name)

#define QUIC_SLOW_TEST(test) QUICHE_SLOW_TEST(test)

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TEST_H_
