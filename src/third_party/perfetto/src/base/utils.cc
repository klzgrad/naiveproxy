/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/ext/base/utils.h"

#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/pipe.h"
#include "perfetto/ext/base/string_utils.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
#include <limits.h>
#include <stdlib.h>  // For _exit()
#include <unistd.h>  // For getpagesize() and geteuid() & fork() & sysconf()
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <mach-o/dyld.h>
#include <mach/vm_page_size.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD)
#include <sys/sysctl.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/prctl.h>

#ifndef PR_GET_TAGGED_ADDR_CTRL
#define PR_GET_TAGGED_ADDR_CTRL 56
#endif

#ifndef PR_TAGGED_ADDR_ENABLE
#define PR_TAGGED_ADDR_ENABLE (1UL << 0)
#endif

#ifndef PR_MTE_TCF_SYNC
#define PR_MTE_TCF_SYNC (1UL << 1)
#endif

#endif  // OS_LINUX | OS_ANDROID

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>

#include <io.h>
#include <malloc.h>  // For _aligned_malloc().
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <dlfcn.h>
#include <malloc.h>

#ifdef M_PURGE
#define PERFETTO_M_PURGE M_PURGE
#else
// Only available in in-tree builds and on newer SDKs.
#define PERFETTO_M_PURGE -101
#endif  // M_PURGE

#ifdef M_PURGE_ALL
#define PERFETTO_M_PURGE_ALL M_PURGE_ALL
#else
// Only available in in-tree builds and on newer SDKs.
#define PERFETTO_M_PURGE_ALL -104
#endif  // M_PURGE

namespace {
extern "C" {
using MalloptType = int (*)(int, int);
}
}  // namespace
#endif  // OS_ANDROID

namespace {

#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)

// Preserve the %rbx register via %rdi to work around a clang bug
// https://bugs.llvm.org/show_bug.cgi?id=17907 (%rbx in an output constraint
// is not considered a clobbered register).
#define PERFETTO_GETCPUID(a, b, c, d, a_inp, c_inp) \
  asm("mov %%rbx, %%rdi\n"                          \
      "cpuid\n"                                     \
      "xchg %%rdi, %%rbx\n"                         \
      : "=a"(a), "=D"(b), "=c"(c), "=d"(d)          \
      : "a"(a_inp), "2"(c_inp))

uint32_t GetXCR0EAX() {
  uint32_t eax = 0, edx = 0;
  asm("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return eax;
}

// If we are building with -msse4 check that the CPU actually supports it.
// This file must be kept in sync with gn/standalone/BUILD.gn.
void PERFETTO_EXPORT_COMPONENT __attribute__((constructor))
CheckCpuOptimizations() {
  uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
  PERFETTO_GETCPUID(eax, ebx, ecx, edx, 1, 0);

  static constexpr uint64_t xcr0_xmm_mask = 0x2;
  static constexpr uint64_t xcr0_ymm_mask = 0x4;
  static constexpr uint64_t xcr0_avx_mask = xcr0_xmm_mask | xcr0_ymm_mask;

  const bool have_popcnt = ecx & (1u << 23);
  const bool have_sse4_2 = ecx & (1u << 20);
  const bool have_avx =
      // Does the OS save/restore XMM and YMM state?
      (ecx & (1u << 27)) &&  // OS support XGETBV.
      (ecx & (1u << 28)) &&  // AVX supported in hardware
      ((GetXCR0EAX() & xcr0_avx_mask) == xcr0_avx_mask);

  // Get level 7 features (eax = 7 and ecx= 0), to check for AVX2 support.
  // (See Intel 64 and IA-32 Architectures Software Developer's Manual
  //  Volume 2A: Instruction Set Reference, A-M CPUID).
  PERFETTO_GETCPUID(eax, ebx, ecx, edx, 7, 0);
  const bool have_avx2 = have_avx && ((ebx >> 5) & 0x1);
  const bool have_bmi = (ebx >> 3) & 0x1;
  const bool have_bmi2 = (ebx >> 8) & 0x1;

  // Get extended features for LZCNT.
  PERFETTO_GETCPUID(eax, ebx, ecx, edx, 0x80000001, 0);
  const bool have_lzcnt = ecx & (1u << 5);

  if (!have_sse4_2 || !have_popcnt || !have_avx2 || !have_bmi || !have_bmi2 ||
      !have_lzcnt) {
    fprintf(
        stderr,
        "This executable requires a x86_64 cpu that supports SSE4.2, BMI2, "
        "AVX2 and LZCNT.\n"
#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
        "On MacOS, this might be caused by running x86_64 binaries on arm64.\n"
        "See https://github.com/google/perfetto/issues/294 for more.\n"
#endif
        "Rebuild with enable_perfetto_x64_cpu_opt=false.\n");
    _exit(126);
  }
}
#endif

}  // namespace

namespace perfetto {
namespace base {

namespace internal {

std::atomic<uint32_t> g_cached_page_size{0};

uint32_t GetSysPageSizeSlowpath() {
  uint32_t page_size = 0;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  const int page_size_int = getpagesize();
  // If sysconf() fails for obscure reasons (e.g. SELinux denial) assume the
  // page size is 4KB. This is to avoid regressing subtle SDK usages, as old
  // versions of this code had a static constant baked in.
  page_size = static_cast<uint32_t>(page_size_int > 0 ? page_size_int : 4096);
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  page_size = static_cast<uint32_t>(vm_page_size);
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD)
  page_size = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));
#else
  page_size = 4096;
#endif

  PERFETTO_CHECK(page_size > 0 && page_size % 4096 == 0);

  // Races here are fine because any thread will write the same value.
  g_cached_page_size.store(page_size, std::memory_order_relaxed);
  return page_size;
}

}  // namespace internal

void MaybeReleaseAllocatorMemToOS() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // mallopt() on Android requires SDK level 26. Many targets and embedders
  // still depend on a lower SDK level. Given mallopt() is a quite simple API,
  // use reflection to do this rather than bumping the SDK level for all
  // embedders. This keeps the behavior of standalone builds aligned with
  // in-tree builds.
  static MalloptType mallopt_fn =
      reinterpret_cast<MalloptType>(dlsym(RTLD_DEFAULT, "mallopt"));
  if (!mallopt_fn)
    return;
  if (mallopt_fn(PERFETTO_M_PURGE_ALL, 0) == 0) {
    mallopt_fn(PERFETTO_M_PURGE, 0);
  }
#endif
}

uid_t GetCurrentUserId() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  return geteuid();
#else
  // TODO(primiano): On Windows we could hash the current user SID and derive a
  // numeric user id [1]. It is not clear whether we need that. Right now that
  // would not bring any benefit. Returning 0 unil we can prove we need it.
  // [1]:https://android-review.googlesource.com/c/platform/external/perfetto/+/1513879/25/src/base/utils.cc
  return 0;
#endif
}

void SetEnv(const std::string& key, const std::string& value) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  PERFETTO_CHECK(::_putenv_s(key.c_str(), value.c_str()) == 0);
#else
  PERFETTO_CHECK(::setenv(key.c_str(), value.c_str(), /*overwrite=*/true) == 0);
#endif
}

void UnsetEnv(const std::string& key) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  PERFETTO_CHECK(::_putenv_s(key.c_str(), "") == 0);
#else
  PERFETTO_CHECK(::unsetenv(key.c_str()) == 0);
#endif
}

void Daemonize(std::function<int()> parent_cb) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) || \
    (PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) &&  \
     !PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE_TVOS))
  Pipe pipe = Pipe::Create(Pipe::kBothBlock);
  pid_t pid;
  switch (pid = fork()) {
    case -1:
      PERFETTO_FATAL("fork");
    case 0: {
      PERFETTO_CHECK(setsid() != -1);
      base::ignore_result(chdir("/"));
      base::ScopedFile null = base::OpenFile("/dev/null", O_RDONLY);
      PERFETTO_CHECK(null);
      PERFETTO_CHECK(dup2(*null, STDIN_FILENO) != -1);
      PERFETTO_CHECK(dup2(*null, STDOUT_FILENO) != -1);
      PERFETTO_CHECK(dup2(*null, STDERR_FILENO) != -1);
      // Do not accidentally close stdin/stdout/stderr.
      if (*null <= 2)
        null.release();
      WriteAll(*pipe.wr, "1", 1);
      break;
    }
    default: {
      // Wait for the child process to have reached the setsid() call. This is
      // to avoid that 'adb shell perfetto -D' destroys the terminal (hence
      // sending a SIGHUP to the child) before the child has detached from the
      // terminal (see b/238644870).

      // This is to unblock the read() below (with EOF, which will fail the
      // CHECK) in the unlikely case of the child crashing before WriteAll("1").
      pipe.wr.reset();
      char one = '\0';
      PERFETTO_CHECK(Read(*pipe.rd, &one, sizeof(one)) == 1 && one == '1');
      printf("%d\n", pid);
      int err = parent_cb();
      exit(err);
    }
  }
#else
  // Avoid -Wunreachable warnings.
  if (reinterpret_cast<intptr_t>(&Daemonize) != 16)
    PERFETTO_FATAL("--background is only supported on Linux/Android/Mac");
  ignore_result(parent_cb);
#endif  // OS_WIN
}

std::string GetCurExecutablePath() {
  std::string self_path;
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)
  char buf[PATH_MAX];
  ssize_t size = readlink("/proc/self/exe", buf, sizeof(buf));
  PERFETTO_CHECK(size != -1);
  // readlink does not null terminate.
  self_path = std::string(buf, static_cast<size_t>(size));
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  uint32_t size = 0;
  PERFETTO_CHECK(_NSGetExecutablePath(nullptr, &size));
  self_path.resize(size);
  PERFETTO_CHECK(_NSGetExecutablePath(&self_path[0], &size) == 0);
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  char buf[MAX_PATH];
  auto len = ::GetModuleFileNameA(nullptr /*current*/, buf, sizeof(buf));
  self_path = std::string(buf, len);
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD)
  char buf[PATH_MAX];
  int mib[4], ret;
  size_t len = sizeof(buf);
  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PATHNAME;
  mib[3] = -1;
  ret = sysctl(mib, 4, buf, &len, NULL, 0);
  PERFETTO_CHECK(ret == 0);
  // This returns the full path; need to trim the executable
  self_path = std::string(buf);
#else
  PERFETTO_FATAL(
      "GetCurExecutableDir() not implemented on the current platform");
#endif
  return self_path;
}

std::string GetCurExecutableDir() {
  auto path = GetCurExecutablePath();
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Paths in Windows can have both kinds of slashes (mingw vs msvc).
  path = path.substr(0, path.find_last_of('\\'));
#endif
  path = path.substr(0, path.find_last_of('/'));
  return path;
}

void* AlignedAlloc(size_t alignment, size_t size) {
  void* res = nullptr;
  alignment = AlignUp<sizeof(void*)>(alignment);  // At least pointer size.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // Window's _aligned_malloc() has a nearly identically signature to Unix's
  // aligned_alloc() but its arguments are obviously swapped.
  res = _aligned_malloc(size, alignment);
#else
  // aligned_alloc() has been introduced in Android only in API 28.
  // Also NaCl and Fuchsia seems to have only posix_memalign().
  ignore_result(posix_memalign(&res, alignment, size));
#endif
  PERFETTO_CHECK(res);
  return res;
}

void AlignedFree(void* ptr) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  _aligned_free(ptr);  // MSDN says it is fine to pass nullptr.
#else
  free(ptr);
#endif
}

bool IsSyncMemoryTaggingEnabled() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // Compute only once per lifetime of the process.
  static bool cached_value = [] {
    const int res = prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0);
    if (res < 0)
      return false;
    const uint32_t actl = static_cast<uint32_t>(res);
    return (actl & PR_TAGGED_ADDR_ENABLE) && (actl & PR_MTE_TCF_SYNC);
  }();
  return cached_value;
#else
  return false;
#endif
}

std::string HexDump(const void* data_void, size_t len, size_t bytes_per_line) {
  const char* data = reinterpret_cast<const char*>(data_void);
  std::string res;
  static const size_t kPadding = bytes_per_line * 3 + 12;
  std::unique_ptr<char[]> line(new char[bytes_per_line * 4 + 128]);
  for (size_t i = 0; i < len; i += bytes_per_line) {
    char* wptr = line.get();
    wptr += base::SprintfTrunc(wptr, 19, "%08zX: ", i);
    for (size_t j = i; j < i + bytes_per_line && j < len; j++) {
      wptr += base::SprintfTrunc(wptr, 4, "%02X ",
                                 static_cast<unsigned>(data[j]) & 0xFF);
    }
    for (size_t j = static_cast<size_t>(wptr - line.get()); j < kPadding; ++j)
      *(wptr++) = ' ';
    for (size_t j = i; j < i + bytes_per_line && j < len; j++) {
      char c = data[j];
      *(wptr++) = (c >= 32 && c < 127) ? c : '.';
    }
    *(wptr++) = '\n';
    *(wptr++) = '\0';
    res.append(line.get());
  }
  return res;
}

}  // namespace base
}  // namespace perfetto
