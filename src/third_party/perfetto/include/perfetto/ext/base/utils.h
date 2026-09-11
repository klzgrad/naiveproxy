/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_BASE_UTILS_H_
#define INCLUDE_PERFETTO_EXT_BASE_UTILS_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/sys_types.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
// Even if Windows has errno.h, the all syscall-restart behavior does not apply.
// Trying to handle EINTR can cause more harm than good if errno is left stale.
// Chromium does the same.
#define PERFETTO_EINTR(x) (x)
#else
#define PERFETTO_EINTR(x)                                   \
  ([&] {                                                    \
    decltype(x) eintr_wrapper_result;                       \
    do {                                                    \
      eintr_wrapper_result = (x);                           \
    } while (eintr_wrapper_result == -1 && errno == EINTR); \
    return eintr_wrapper_result;                            \
  }())
#endif

namespace perfetto {
namespace base {

namespace internal {
extern std::atomic<uint32_t> g_cached_page_size;
uint32_t GetSysPageSizeSlowpath();
}  // namespace internal

// Returns the system's page size. Use this when dealing with mmap, madvise and
// similar mm-related syscalls.
// This function might be called in hot paths. Avoid calling getpagesize() all
// the times, in many implementations getpagesize() calls sysconf() which is
// not cheap.
inline uint32_t GetSysPageSize() {
  const uint32_t page_size =
      internal::g_cached_page_size.load(std::memory_order_relaxed);
  return page_size != 0 ? page_size : internal::GetSysPageSizeSlowpath();
}

template <typename T, size_t TSize>
constexpr size_t ArraySize(const T (&)[TSize]) {
  return TSize;
}

// Adds `a` and `b` into `*result`, returning false on overflow. The value of
// `*result` is unspecified when false is returned.
inline bool CheckedAdd(int64_t a, int64_t b, int64_t* result) {
#if defined(__clang__) || defined(__GNUC__)
  return !__builtin_add_overflow(a, b, result);
#else
  constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
  constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
  if ((b > 0 && a > kMax - b) || (b < 0 && a < kMin - b))
    return false;
  *result = a + b;
  return true;
#endif
}

// Returns whether `value` is positive or negative zero. Compares the bit
// pattern so it can be used in translation units compiled with -Wfloat-equal.
inline bool IsZero(double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return (bits << 1) == 0;
}
inline bool IsZero(int64_t value) {
  return value == 0;
}

// Adds `a` and `b`, clamping to INT64_MIN / INT64_MAX on overflow instead of
// wrapping (which is UB and can flip the sign of the result).
inline int64_t SaturatingAdd(int64_t a, int64_t b) {
  constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
  constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
#if defined(__clang__) || defined(__GNUC__)
  int64_t result;
  if (PERFETTO_UNLIKELY(__builtin_add_overflow(a, b, &result)))
    return a < 0 ? kMin : kMax;
  return result;
#else
  if (b > 0 && a > kMax - b)
    return kMax;
  if (b < 0 && a < kMin - b)
    return kMin;
  return a + b;
#endif
}

// Multiplies `a` by `b`, clamping to INT64_MIN / INT64_MAX on overflow instead
// of wrapping (which is UB and can flip the sign of the result).
inline int64_t SaturatingMultiply(int64_t a, int64_t b) {
  constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
  constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
#if defined(__clang__) || defined(__GNUC__)
  int64_t result;
  if (PERFETTO_UNLIKELY(__builtin_mul_overflow(a, b, &result))) {
    // On overflow the true product's sign is the XOR of the operands' signs.
    // Neither operand can be zero here (0 never overflows).
    return ((a < 0) == (b < 0)) ? kMax : kMin;
  }
  return result;
#else
  // Portable fallback (e.g. MSVC cl.exe): detect overflow *before* multiplying
  // by dividing a limit by one operand, which never itself overflows.
  if (a == 0 || b == 0)
    return 0;
  if ((a < 0) == (b < 0)) {  // Same sign: product is positive.
    if (a > 0 ? (a > kMax / b) : (a < kMax / b))
      return kMax;
  } else {  // Opposite signs: product is negative.
    if (a > 0 ? (b < kMin / a) : (a < kMin / b))
      return kMin;
  }
  return a * b;
#endif
}

// Function object which invokes 'free' on its parameter, which must be
// a pointer. Can be used to store malloc-allocated pointers in std::unique_ptr:
//
// std::unique_ptr<int, base::FreeDeleter> foo_ptr(
//     static_cast<int*>(malloc(sizeof(int))));
struct FreeDeleter {
  inline void operator()(void* ptr) const { free(ptr); }
};

template <typename T>
constexpr T AssumeLittleEndian(T value) {
#if !PERFETTO_IS_LITTLE_ENDIAN()
  static_assert(false, "Unimplemented on big-endian archs");
#endif
  return value;
}

// Round up |size| to a multiple of |alignment| (must be a power of two).
inline constexpr size_t AlignUp(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

// Round down |size| to a multiple of |alignment| (must be a power of two).
inline constexpr size_t AlignDown(size_t size, size_t alignment) {
  return size & ~(alignment - 1);
}

template <typename T>
inline constexpr bool IsPowerOfTwo(T x) {
  static_assert(std::is_unsigned_v<T> && std::is_integral_v<T>,
                "T must be an unsigned integer");
  return x != 0 && (x & (x - 1)) == 0;
}

// Returns the smallest power of two greater than or equal to |x|. Returns zero
// for zero or when the result is not representable by T.
template <typename T>
inline constexpr T RoundUpToPowerOfTwo(T x) {
  static_assert(std::is_unsigned_v<T> && std::is_integral_v<T>,
                "T must be an unsigned integer");
  if (x == 0) {
    return 0;
  }
  --x;
  for (size_t shift = 1; shift < sizeof(T) * 8; shift *= 2) {
    x |= x >> shift;
  }
  return ++x;
}

// TODO(primiano): clean this up and move all existing usages to the constexpr
// version above.
template <size_t alignment>
constexpr size_t AlignUp(size_t size) {
  static_assert(IsPowerOfTwo(alignment), "alignment must be a pow2");
  return AlignUp(size, alignment);
}

inline bool IsAgain(int err) {
  return err == EAGAIN || err == EWOULDBLOCK;
}

// setenv(2)-equivalent. Deals with Windows vs Posix discrepancies.
void SetEnv(const std::string& key, const std::string& value);

// unsetenv(2)-equivalent. Deals with Windows vs Posix discrepancies.
void UnsetEnv(const std::string& key);

// Returns true if |fd| is connected to an interactive terminal (TTY). Deals
// with Windows vs Posix discrepancies (isatty() vs _isatty()).
bool IsTty(int fd);

// Convenience overload for C stdio streams (e.g. stdin/stdout/stderr).
bool IsTty(FILE* stream);

// Calls mallopt(M_PURGE, 0) on Android. Does nothing on other platforms.
// This forces the allocator to release freed memory. This is used to work
// around various Scudo inefficiencies. See b/170217718.
void MaybeReleaseAllocatorMemToOS();

// geteuid() on POSIX OSes, returns 0 on Windows (See comment in utils.cc).
uid_t GetCurrentUserId();

// Forks the process.
// Parent: calls |parent_cb| with the child's PID and exits with its return
//         value. The callback owns any startup output (e.g. printing the PID).
// Child: redirects stdio onto /dev/null, chdirs into / and returns.
void Daemonize(std::function<int(pid_t)> parent_cb);

// Returns the path of the current executable, e.g. /foo/bar/exe.
std::string GetCurExecutablePath();

// Returns the directory where the current executable lives in, e.g. /foo/bar.
// This is independent of cwd().
std::string GetCurExecutableDir();

// Memory returned by AlignedAlloc() must be freed via AlignedFree() not just
// free. It makes a difference on Windows where _aligned_malloc() and
// _aligned_free() must be paired.
// Prefer using the AlignedAllocTyped() below which takes care of the pairing.
void* AlignedAlloc(size_t alignment, size_t size);
void AlignedFree(void*);

// Detects Sync-mode MTE (currently being tested in some Android builds).
// This is known to use extra memory for the stack history buffer.
bool IsSyncMemoryTaggingEnabled();

// A RAII version of the above, which takes care of pairing Aligned{Alloc,Free}.
template <typename T>
struct AlignedDeleter {
  inline void operator()(T* ptr) const { AlignedFree(ptr); }
};

// The remove_extent<T> here and below is to allow defining unique_ptr<T[]>.
// As per https://en.cppreference.com/w/cpp/memory/unique_ptr the Deleter takes
// always a T*, not a T[]*.
template <typename T>
using AlignedUniquePtr =
    std::unique_ptr<T, AlignedDeleter<typename std::remove_extent<T>::type>>;

template <typename T>
AlignedUniquePtr<T> AlignedAllocTyped(size_t n_membs) {
  using TU = typename std::remove_extent<T>::type;
  return AlignedUniquePtr<T>(
      static_cast<TU*>(AlignedAlloc(alignof(TU), sizeof(TU) * n_membs)));
}

// A RAII wrapper to invoke a function when leaving a function/scope.
template <typename Func>
class OnScopeExitWrapper {
 public:
  explicit OnScopeExitWrapper(Func f) : f_(std::move(f)), active_(true) {}
  OnScopeExitWrapper(OnScopeExitWrapper&& other) noexcept
      : f_(std::move(other.f_)), active_(other.active_) {
    other.active_ = false;
  }
  ~OnScopeExitWrapper() {
    if (active_)
      f_();
  }

 private:
  Func f_;
  bool active_;
};

template <typename Func>
PERFETTO_WARN_UNUSED_RESULT OnScopeExitWrapper<Func> OnScopeExit(Func f) {
  return OnScopeExitWrapper<Func>(std::move(f));
}

// Returns a xxd-style hex dump (hex + ascii chars) of the input data.
std::string HexDump(const void* data, size_t len, size_t bytes_per_line = 16);
inline std::string HexDump(const std::string& data,
                           size_t bytes_per_line = 16) {
  return HexDump(data.data(), data.size(), bytes_per_line);
}

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_UTILS_H_
