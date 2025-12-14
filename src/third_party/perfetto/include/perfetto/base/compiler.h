/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_PERFETTO_BASE_COMPILER_H_
#define INCLUDE_PERFETTO_BASE_COMPILER_H_

#include <cstddef>
#include <type_traits>
#include <variant>

#include "perfetto/public/compiler.h"

#if defined(_MSC_VER)
#define PERFETTO_ASSUME(x) __assume(x)
#elif defined(__clang__)
#define PERFETTO_ASSUME(x) __builtin_assume(x)
#else
#define PERFETTO_ASSUME(x)     \
  do {                         \
    if (!(x))                  \
      __builtin_unreachable(); \
  } while (0)
#endif

// __has_attribute is supported only by clang and recent versions of GCC.
// Add a layer to wrap the __has_attribute macro.
#if defined(__has_attribute)
#define PERFETTO_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#define PERFETTO_HAS_ATTRIBUTE(x) 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define PERFETTO_WARN_UNUSED_RESULT
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_UNUSED __attribute__((unused))
#else
#define PERFETTO_UNUSED
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_NORETURN __attribute__((__noreturn__))
#else
#define PERFETTO_NORETURN __declspec(noreturn)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_DEBUG_FUNCTION_IDENTIFIER() __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define PERFETTO_DEBUG_FUNCTION_IDENTIFIER() __FUNCSIG__
#else
#define PERFETTO_DEBUG_FUNCTION_IDENTIFIER() \
  static_assert(false, "Not implemented for this compiler")
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_PRINTF_FORMAT(x, y) \
  __attribute__((__format__(__printf__, x, y)))
#else
#define PERFETTO_PRINTF_FORMAT(x, y)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_POPCOUNT(x) __builtin_popcountll(x)
#else
#include <intrin.h>
#define PERFETTO_POPCOUNT(x) __popcnt64(x)
#endif

#if defined(__clang__)
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" void __asan_poison_memory_region(void const volatile*, size_t);
extern "C" void __asan_unpoison_memory_region(void const volatile*, size_t);
#define PERFETTO_ASAN_POISON(a, s) __asan_poison_memory_region((a), (s))
#define PERFETTO_ASAN_UNPOISON(a, s) __asan_unpoison_memory_region((a), (s))
#else
#define PERFETTO_ASAN_POISON(addr, size)
#define PERFETTO_ASAN_UNPOISON(addr, size)
#endif  // __has_feature(address_sanitizer)
#else
#define PERFETTO_ASAN_POISON(addr, size)
#define PERFETTO_ASAN_UNPOISON(addr, size)
#endif  // __clang__

#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_IS_LITTLE_ENDIAN() __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#else
// Assume all MSVC targets are little endian.
#define PERFETTO_IS_LITTLE_ENDIAN() 1
#endif

// This is used for exporting xxxMain() symbols (e.g., PerfettoCmdMain,
// ProbesMain) from libperfetto.so when the GN arg monolithic_binaries = false.
#if defined(__GNUC__) || defined(__clang__)
#define PERFETTO_EXPORT_ENTRYPOINT __attribute__((visibility("default")))
#else
// TODO(primiano): on Windows this should be a pair of dllexport/dllimport. But
// that requires a -DXXX_IMPLEMENTATION depending on whether we are on the
// impl-site or call-site. Right now it's not worth the trouble as we
// force-export the xxxMain() symbols only on Android, where we pack all the
// code for N binaries into one .so to save binary size. On Windows we support
// only monolithic binaries, as they are easier to deal with.
#define PERFETTO_EXPORT_ENTRYPOINT
#endif

// Disables undefined behavior analysis for a function.
#if defined(__clang__)
#define PERFETTO_NO_SANITIZE_UNDEFINED __attribute__((no_sanitize("undefined")))
#else
#define PERFETTO_NO_SANITIZE_UNDEFINED
#endif

// Avoid calling the exit-time destructor on an object with static lifetime.
#if PERFETTO_HAS_ATTRIBUTE(no_destroy)
#define PERFETTO_HAS_NO_DESTROY() 1
#define PERFETTO_NO_DESTROY __attribute__((no_destroy))
#else
#define PERFETTO_HAS_NO_DESTROY() 0
#define PERFETTO_NO_DESTROY
#endif

// Macro for telling -Wimplicit-fallthrough that a fallthrough is intentional.
#define PERFETTO_FALLTHROUGH [[fallthrough]]

// Depending on the version of the compiler, __has_builtin can be provided or
// not.
#if defined(__has_builtin)
#if __has_builtin(__builtin_stack_address)
#define PERFETTO_HAS_BUILTIN_STACK_ADDRESS() 1
#else
#define PERFETTO_HAS_BUILTIN_STACK_ADDRESS() 0
#endif
#else
#define PERFETTO_HAS_BUILTIN_STACK_ADDRESS() 0
#endif

namespace perfetto::base {

template <typename... T>
inline void ignore_result(const T&...) {}

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_BASE_COMPILER_H_
