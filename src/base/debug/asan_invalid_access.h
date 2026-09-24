// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Defines some functions that intentionally do an invalid memory access in
// order to trigger an AddressSanitizer (ASan) error report.

#ifndef BASE_DEBUG_ASAN_INVALID_ACCESS_H_
#define BASE_DEBUG_ASAN_INVALID_ACCESS_H_

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/sanitizer_buildflags.h"
#include "build/build_config.h"

namespace base::debug {

// Generates an heap buffer overflow.
NOINLINE BASE_EXPORT void AsanHeapOverflow();

// Generates an heap buffer underflow.
NOINLINE BASE_EXPORT void AsanHeapUnderflow();

// Generates a use-after-free by allocating a heap array, freeing it, and then
// attempting to read from the freed memory.
NOINLINE BASE_EXPORT void AsanHeapUseAfterFree();

// Generates a use-after-free by dereferencing a pointer that points to a member
// of a freed allocation. Specifically, it allocates a pointer-to-pointer on
// the heap pointing to a stack variable, frees the heap allocation, reads the
// stack variable address from the freed heap memory (UaF), and then
// dereferences that address.
//
// This is useful when debugging zapping-based memory protections like
// MiracleObject. The inner pointer gets overwritten with the zapping pattern
// and invalidated upon quarantine. Dereferencing the zapped pointer results in
// a deterministic crash.
NOINLINE BASE_EXPORT void AsanHeapMemberDereferenceAfterFree();

#if defined(ADDRESS_SANITIZER) || BUILDFLAG(IS_HWASAN)
// The "corrupt-block" and "corrupt-heap" classes of bugs is specific to
// Windows.
#if BUILDFLAG(IS_WIN)
// Corrupts a memory block and makes sure that the corruption gets detected when
// we try to free this block.
NOINLINE BASE_EXPORT void AsanCorruptHeapBlock();

// Corrupts the heap and makes sure that the corruption gets detected when a
// crash occur.
NOINLINE BASE_EXPORT void AsanCorruptHeap();

#endif  // BUILDFLAG(IS_WIN)
#endif  // ADDRESS_SANITIZER

}  // namespace base::debug

#endif  // BASE_DEBUG_ASAN_INVALID_ACCESS_H_
