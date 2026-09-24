/*
 * Copyright (C) 2026 The Android Open Source Project
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

// Startup check that the CPU supports the extensions (SSE4.2, AVX2, BMI2,
// LZCNT) the binary is built with when enable_perfetto_x64_cpu_opt is set; if
// not, it prints an error and exits.
//
// Must be built for the baseline x86_64 ISA (its target subtracts
// config("x64_cpu_opt") in src/base/BUILD.gn), else the check could itself use
// instructions the CPU lacks. Context:
// https://github.com/google/perfetto/discussions/5138

#include <stdint.h>

// The __attribute__((constructor)) below is intentional: this function runs at
// load time on purpose to verify the CPU supports the extensions the binary is
// built with. -Wglobal-constructors (enabled by -Weverything) would otherwise
// flag it.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif

#include "perfetto/base/build_config.h"
#include "perfetto/base/export.h"

#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)

#include <stdio.h>
#include <unistd.h>  // For _exit().

namespace {

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

}  // namespace

#endif  // PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
