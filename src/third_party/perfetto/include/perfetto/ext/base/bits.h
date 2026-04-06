/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_BITS_H_
#define INCLUDE_PERFETTO_EXT_BASE_BITS_H_

#include <cstddef>
#include <cstdint>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"

#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
#include <immintrin.h>
#endif

namespace perfetto {
namespace base {

inline PERFETTO_ALWAYS_INLINE uint32_t CountLeadZeros32(uint32_t value) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  return static_cast<uint32_t>(_lzcnt_u32(value));
#elif defined(__GNUC__) || defined(__clang__)
  return value ? static_cast<uint32_t>(__builtin_clz(value)) : 32u;
#else
  unsigned long out;
  return _BitScanReverse(&out, value) ? 31 - out : 32u;
#endif
}

inline PERFETTO_ALWAYS_INLINE uint32_t CountLeadZeros64(uint64_t value) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  return static_cast<uint32_t>(_lzcnt_u64(value));
#elif defined(__GNUC__) || defined(__clang__)
  return value ? static_cast<uint32_t>(__builtin_clzll(value)) : 64u;
#else
  unsigned long out;
  return _BitScanReverse64(&out, value) ? 63 - out : 64u;
#endif
}

template <typename T>
uint32_t CountLeadZeros(T value) {
  if constexpr (sizeof(T) == 8)
    return CountLeadZeros64(static_cast<uint64_t>(value));
  return CountLeadZeros32(static_cast<uint32_t>(value));
}

inline PERFETTO_ALWAYS_INLINE uint32_t CountTrailZeros64(uint64_t value) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  return static_cast<uint32_t>(_tzcnt_u64(value));
#elif defined(__GNUC__) || defined(__clang__)
  return value ? static_cast<uint32_t>(__builtin_ctzll(value)) : 64u;
#else
  unsigned long out;
  return _BitScanForward64(&out, value) ? static_cast<uint32_t>(out) : 64u;
#endif
}

inline PERFETTO_ALWAYS_INLINE uint32_t CountTrailZeros32(uint32_t value) {
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  return _tzcnt_u32(value);
#elif defined(__GNUC__) || defined(__clang__)
  return value ? static_cast<uint32_t>(__builtin_ctz(value)) : 32u;
#else
  unsigned long out;
  return _BitScanForward(&out, value) ? out : 32u;
#endif
}

template <typename T>
inline PERFETTO_ALWAYS_INLINE uint32_t CountTrailZeros(T value) {
  if (sizeof(T) == 8) {
    return CountTrailZeros64(static_cast<uint64_t>(value));
  }
  return CountTrailZeros32(static_cast<uint32_t>(value));
}

template <typename T>
constexpr bool AllBitsSet(T v) {
  return v == static_cast<T>(-1);
}

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_BITS_H_
