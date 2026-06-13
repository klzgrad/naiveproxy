// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_IOVEC_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_IOVEC_IMPL_H_

#include "quiche/common/platform/api/quiche_export.h"

#if defined(_WIN32)

// See <https://pubs.opengroup.org/onlinepubs/009604599/basedefs/sys/uio.h.html>
struct QUICHE_EXPORT iovec {
  void* iov_base;
  size_t iov_len;
};

#else

#include <sys/uio.h>  // IWYU pragma: export

#endif  // defined(_WIN32)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_IOVEC_IMPL_H_
