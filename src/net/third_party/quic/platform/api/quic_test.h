// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_TEST_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_TEST_H_

#include "net/third_party/quic/platform/impl/quic_test_impl.h"

using QuicFlagSaver = QuicFlagSaverImpl;

// Defines the base classes to be used in QUIC tests.
using QuicTest = QuicTestImpl;
template <class T>
using QuicTestWithParam = QuicTestWithParamImpl<T>;

// Class which needs to be instantiated in tests which use threads.
using ScopedEnvironmentForThreads = ScopedEnvironmentForThreadsImpl;

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_TEST_H_
