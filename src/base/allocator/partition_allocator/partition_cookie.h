// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_COOKIE_H_
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_COOKIE_H_

#include "base/allocator/buildflags.h"
#include "base/allocator/partition_allocator/partition_alloc_check.h"
#include "base/compiler_specific.h"

namespace base {
namespace internal {

// Handles alignment up to XMM instructions on Intel.
static constexpr size_t kCookieSize = 16;

// Cookies are enabled for debug builds, unless PartitionAlloc is used as the
// malloc() implementation. This is a temporary workaround the alignment issues
// caused by cookies. With them, PartitionAlloc cannot support posix_memalign(),
// which is required.
//
// TODO(lizeb): Support cookies when used as the malloc() implementation.
#if DCHECK_IS_ON() && !BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

static constexpr unsigned char kCookieValue[kCookieSize] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xD0, 0x0D,
    0x13, 0x37, 0xF0, 0x05, 0xBA, 0x11, 0xAB, 0x1E};

ALWAYS_INLINE void PartitionCookieCheckValue(void* ptr) {
  unsigned char* cookie_ptr = reinterpret_cast<unsigned char*>(ptr);
  for (size_t i = 0; i < kCookieSize; ++i, ++cookie_ptr)
    PA_DCHECK(*cookie_ptr == kCookieValue[i]);
}

ALWAYS_INLINE size_t PartitionCookieSizeAdjustAdd(size_t size) {
  // Add space for cookies, checking for integer overflow. TODO(palmer):
  // Investigate the performance and code size implications of using
  // CheckedNumeric throughout PA.
  PA_DCHECK(size + (2 * kCookieSize) > size);
  size += 2 * kCookieSize;
  return size;
}

ALWAYS_INLINE void* PartitionCookieFreePointerAdjust(void* ptr) {
  // The value given to the application is actually just after the cookie.
  ptr = static_cast<char*>(ptr) - kCookieSize;
  return ptr;
}

ALWAYS_INLINE size_t PartitionCookieSizeAdjustSubtract(size_t size) {
  // Remove space for cookies.
  PA_DCHECK(size >= 2 * kCookieSize);
  size -= 2 * kCookieSize;
  return size;
}

ALWAYS_INLINE size_t PartitionCookieOffsetSubtract(size_t offset) {
#if DCHECK_IS_ON()
  // Convert offset from the beginning of the allocated slot to offset from
  // the value given to the application, which is just after the cookie.
  offset -= kCookieSize;
#endif
  return offset;
}

ALWAYS_INLINE void PartitionCookieWriteValue(void* ptr) {
  unsigned char* cookie_ptr = reinterpret_cast<unsigned char*>(ptr);
  for (size_t i = 0; i < kCookieSize; ++i, ++cookie_ptr)
    *cookie_ptr = kCookieValue[i];
}

#else

ALWAYS_INLINE void PartitionCookieCheckValue(void* ptr) {}

ALWAYS_INLINE size_t PartitionCookieSizeAdjustAdd(size_t size) {
  return size;
}

ALWAYS_INLINE void* PartitionCookieFreePointerAdjust(void* ptr) {
  return ptr;
}

ALWAYS_INLINE size_t PartitionCookieSizeAdjustSubtract(size_t size) {
  return size;
}

ALWAYS_INLINE void PartitionCookieWriteValue(void* ptr) {}
#endif  // DCHECK_IS_ON() && !BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

}  // namespace internal
}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_PARTITION_COOKIE_H_
