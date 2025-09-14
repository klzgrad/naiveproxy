// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_IOVEC_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_IOVEC_H_

#include <cstddef>
#include <type_traits>

#include "quiche_platform_impl/quiche_iovec_impl.h"

// The impl header has to export struct iovec, or a POSIX-compatible polyfill.
// Below, we mostly assert that what we have is appropriate.
static_assert(std::is_standard_layout<struct iovec>::value,
              "iovec has to be a standard-layout struct");

static_assert(offsetof(struct iovec, iov_base) < sizeof(struct iovec),
              "iovec has to have iov_base");
static_assert(offsetof(struct iovec, iov_len) < sizeof(struct iovec),
              "iovec has to have iov_len");

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_IOVEC_H_
