// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cpu_features_macros.h"

#ifdef CPU_FEATURES_ARCH_AARCH64
#if (defined(CPU_FEATURES_OS_FREEBSD) || defined(CPU_FEATURES_OS_OPENBSD) || \
     defined(CPU_FEATURES_OS_LINUX) || defined(CPU_FEATURES_OS_ANDROID))
#if (defined(CPU_FEATURES_COMPILER_GCC) || defined(CPU_FEATURES_COMPILER_CLANG))

#include "internal/cpuid_aarch64.h"

#ifdef CPU_FEATURES_MOCK_CPUID_AARCH64
// Implementation will be provided by test/cpuinfo_aarch64_test.cc.
#else
uint64_t GetMidrEl1(void) {
  uint64_t midr_el1;
  __asm("mrs %0, MIDR_EL1" : "=r"(midr_el1));
  return midr_el1;
}
#endif  // CPU_FEATURES_MOCK_CPUID_AARCH64

#else
#error "Unsupported compiler, aarch64 cpuid requires either GCC or Clang."
#endif  // (defined(CPU_FEATURES_COMPILER_GCC) ||
        // defined(CPU_FEATURES_COMPILER_CLANG))
#endif  // (defined(CPU_FEATURES_OS_FREEBSD) || defined(CPU_FEATURES_OS_OPENBSD)
        // || defined(CPU_FEATURES_OS_LINUX) || defined(CPU_FEATURES_OS_ANDROID))
#endif  // CPU_FEATURES_ARCH_AARCH64
