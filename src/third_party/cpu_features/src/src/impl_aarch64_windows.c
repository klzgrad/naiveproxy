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
#ifdef CPU_FEATURES_OS_WINDOWS

#include <stdbool.h>

#include "cpuinfo_aarch64.h"
#include "impl_aarch64__base_implementation.inl"
#include "internal/windows_utils.h"

#ifdef CPU_FEATURES_MOCK_CPUID_AARCH64
extern bool GetWindowsIsProcessorFeaturePresent(DWORD);
extern WORD GetWindowsNativeSystemInfoProcessorRevision();
#else  // CPU_FEATURES_MOCK_CPUID_AARCH64
static bool GetWindowsIsProcessorFeaturePresent(DWORD dwProcessorFeature) {
  return IsProcessorFeaturePresent(dwProcessorFeature);
}

static WORD GetWindowsNativeSystemInfoProcessorRevision() {
  SYSTEM_INFO system_info;
  GetNativeSystemInfo(&system_info);
  return system_info.wProcessorRevision;
}
#endif

static const Aarch64Info kEmptyAarch64Info;

Aarch64Info GetAarch64Info(void) {
  Aarch64Info info = kEmptyAarch64Info;
  info.revision = GetWindowsNativeSystemInfoProcessorRevision();
  info.features.fp =
      GetWindowsIsProcessorFeaturePresent(PF_ARM_VFP_32_REGISTERS_AVAILABLE);
  info.features.asimd =
      GetWindowsIsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);
  info.features.crc32 = GetWindowsIsProcessorFeaturePresent(
      PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE);
  info.features.asimddp =
      GetWindowsIsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE);
  info.features.jscvt = GetWindowsIsProcessorFeaturePresent(
      PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE);
  info.features.lrcpc = GetWindowsIsProcessorFeaturePresent(
      PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE);
  info.features.atomics = GetWindowsIsProcessorFeaturePresent(
      PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE);

  bool is_crypto_available = GetWindowsIsProcessorFeaturePresent(
      PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
  info.features.aes = is_crypto_available;
  info.features.sha1 = is_crypto_available;
  info.features.sha2 = is_crypto_available;
  info.features.pmull = is_crypto_available;
  return info;
}

#endif  // CPU_FEATURES_OS_WINDOWS
#endif  // CPU_FEATURES_ARCH_AARCH64
