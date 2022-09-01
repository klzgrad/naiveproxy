// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_PREFETCH_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_PREFETCH_IMPL_H_

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace quiche {

inline void QuichePrefetchT0Impl(const void* addr) {
#if !defined(DISABLE_BUILTIN_PREFETCH)
#if defined(__GNUC__) || (defined(_M_ARM64) && defined(__clang__))
  __builtin_prefetch(addr, 0, 3);
#elif defined(_MSC_VER)
  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0);
#else
  (void*)addr;
#endif
#endif  // !defined(DISABLE_BUILTIN_PREFETCH)
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_PREFETCH_IMPL_H_
