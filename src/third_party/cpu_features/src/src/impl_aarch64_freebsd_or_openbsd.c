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
#if defined(CPU_FEATURES_OS_FREEBSD) || defined(CPU_FEATURES_OS_OPENBSD)

#include "cpuinfo_aarch64.h"
#include "impl_aarch64__base_implementation.inl"
#include "internal/cpuid_aarch64.h"
#include "internal/hwcaps.h"

static const Aarch64Info kEmptyAarch64Info;

Aarch64Info GetAarch64Info(void) {
  Aarch64Info info = kEmptyAarch64Info;
  const HardwareCapabilities hwcaps = CpuFeatures_GetHardwareCapabilities();
  for (size_t i = 0; i < AARCH64_LAST_; ++i) {
    if (CpuFeatures_IsHwCapsSet(kHardwareCapabilities[i], hwcaps)) {
      kSetters[i](&info.features, true);
    }
  }
  if (info.features.cpuid) {
    const uint64_t midr_el1 = GetMidrEl1();
    info.implementer = (int)ExtractBitRange(midr_el1, 31, 24);
    info.variant = (int)ExtractBitRange(midr_el1, 23, 20);
    info.part = (int)ExtractBitRange(midr_el1, 15, 4);
    info.revision = (int)ExtractBitRange(midr_el1, 3, 0);
  }
  return info;
}

#endif  // CPU_FEATURES_OS_FREEBSD || CPU_FEATURES_OS_OPENBSD
#endif  // CPU_FEATURES_ARCH_AARCH64
