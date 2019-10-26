// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides the definition of API_AVAILABLE while we're on an SDK that doesn't
// contain it yet.
// TODO(thakis): Remove this file once we're on the 10.12 SDK.

#ifndef BASE_MAC_AVAILABILITY_H_
#define BASE_MAC_AVAILABILITY_H_

#include <AvailabilityMacros.h>

#if !defined(MAC_OS_X_VERSION_10_12) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_12
#define __API_AVAILABLE_PLATFORM_macos(x) macos, introduced = x
#define __API_AVAILABLE_PLATFORM_macosx(x) macosx, introduced = x
#define __API_AVAILABLE_PLATFORM_ios(x) ios, introduced = x
#define __API_AVAILABLE_PLATFORM_watchos(x) watchos, introduced = x
#define __API_AVAILABLE_PLATFORM_tvos(x) tvos, introduced = x
#define __API_A(x) __attribute__((availability(__API_AVAILABLE_PLATFORM_##x)))
#define __API_AVAILABLE1(x) __API_A(x)
#define __API_AVAILABLE2(x, y) __API_A(x) __API_A(y)
#define __API_AVAILABLE3(x, y, z) __API_A(x) __API_A(y) __API_A(z)
#define __API_AVAILABLE4(x, y, z, t) __API_A(x) __API_A(y) __API_A(z) __API_A(t)
#define __API_AVAILABLE_GET_MACRO(_1, _2, _3, _4, NAME, ...) NAME
#define API_AVAILABLE(...)                                                   \
  __API_AVAILABLE_GET_MACRO(__VA_ARGS__, __API_AVAILABLE4, __API_AVAILABLE3, \
                            __API_AVAILABLE2, __API_AVAILABLE1)              \
  (__VA_ARGS__)
#else
#import <os/availability.h>
#endif  // MAC_OS_X_VERSION_10_12

#endif  // BASE_MAC_AVAILABILITY_H_
