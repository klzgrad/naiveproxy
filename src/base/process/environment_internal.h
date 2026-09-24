// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains internal routines that are called by other files in
// base/process/.

#ifndef BASE_PROCESS_ENVIRONMENT_INTERNAL_H_
#define BASE_PROCESS_ENVIRONMENT_INTERNAL_H_

#include "base/base_export.h"
#include "base/containers/heap_array.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "build/build_config.h"

namespace base {
namespace internal {

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
// Returns a modified environment vector constructed from the given environment
// and the list of changes given in |changes|. Each key in the environment is
// matched against the first element of the pairs. In the event of a match, the
// value is replaced by the second of the pair, unless the second is empty, in
// which case the key-value is removed.
//
// This POSIX version takes a span over a POSIX-style environment block and
// returns a new POSIX-style environment block. Each string in the input span
// must be NUL-terminated, and the span must not contain null pointers.
// The input span `env` may be empty.
//
// This function is marked `UNSAFE_BUFFER_USAGE` because it relies on the
// caller-guaranteed precondition that every string in `env` is NUL-terminated.
// The function cannot verify this precondition at compile time or safely at
// runtime, and passing non-NUL-terminated strings will result in
// out-of-bounds memory reads (undefined behavior)
// The returned HeapArray contains a null-terminated list of pointers to
// NUL-terminated strings, followed by the storage for the strings themselves.
// Since the storage is owned by the HeapArray, you cannot copy the pointers
// without keeping the HeapArray alive.
UNSAFE_BUFFER_USAGE BASE_EXPORT base::HeapArray<char*> AlterEnvironment(
    base::span<const char* const> env,
    const EnvironmentMap& changes);

// Returns a span over the current process' environment (excluding the
// terminating null pointer). Each string in the span is NUL-terminated.
// On POSIX, this returns a view into the system's 'environ' array.
BASE_EXPORT base::span<const char* const> GetEnvironment();

// Sets the current process' environment.
// On POSIX, this updates the system's 'environ' array.
BASE_EXPORT void SetEnvironment(base::span<char*> env);
#elif BUILDFLAG(IS_WIN)
// Returns a modified environment vector constructed from the given environment
// and the list of changes given in |changes|. Each key in the environment is
// matched against the first element of the pairs. In the event of a match, the
// value is replaced by the second of the pair, unless the second is empty, in
// which case the key-value is removed.
//
// This Windows version takes a span over a Windows-style environment block and
// returns a new Windows-style environment block, which is a string containing
// several NUL-terminated strings followed by an extra terminating NUL
// character. So, e.g., the environment A=1 B=2 is represented as
// L"A=1\0B=2\0\0". The input span `env` can be empty.
BASE_EXPORT NativeEnvironmentString
AlterEnvironment(base::span<const wchar_t> env, const EnvironmentMap& changes);

// Returns a double-NUL-terminated copy of the current process' environment
// block. Windows requires returning an owned copy of the environment block.
// `GetEnvironmentStrings` allocates memory that the caller must release by
// calling `FreeEnvironmentStrings` (see
// https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstrings).
// To manage this safely, we copy the block to a `HeapArray` and free the
// OS-allocated block immediately.
BASE_EXPORT base::HeapArray<wchar_t> GetEnvironment();
#endif  // OS_*

}  // namespace internal
}  // namespace base

#endif  // BASE_PROCESS_ENVIRONMENT_INTERNAL_H_
