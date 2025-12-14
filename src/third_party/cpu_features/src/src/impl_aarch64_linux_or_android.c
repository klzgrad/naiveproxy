// Copyright 2017 Google LLC
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
#if defined(CPU_FEATURES_OS_LINUX) || defined(CPU_FEATURES_OS_ANDROID)

#include "cpuinfo_aarch64.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions for introspection.
////////////////////////////////////////////////////////////////////////////////
#define INTROSPECTION_TABLE                                                \
  LINE(AARCH64_FP, fp, "fp", AARCH64_HWCAP_FP, 0)                          \
  LINE(AARCH64_ASIMD, asimd, "asimd", AARCH64_HWCAP_ASIMD, 0)              \
  LINE(AARCH64_EVTSTRM, evtstrm, "evtstrm", AARCH64_HWCAP_EVTSTRM, 0)      \
  LINE(AARCH64_AES, aes, "aes", AARCH64_HWCAP_AES, 0)                      \
  LINE(AARCH64_PMULL, pmull, "pmull", AARCH64_HWCAP_PMULL, 0)              \
  LINE(AARCH64_SHA1, sha1, "sha1", AARCH64_HWCAP_SHA1, 0)                  \
  LINE(AARCH64_SHA2, sha2, "sha2", AARCH64_HWCAP_SHA2, 0)                  \
  LINE(AARCH64_CRC32, crc32, "crc32", AARCH64_HWCAP_CRC32, 0)              \
  LINE(AARCH64_ATOMICS, atomics, "atomics", AARCH64_HWCAP_ATOMICS, 0)      \
  LINE(AARCH64_FPHP, fphp, "fphp", AARCH64_HWCAP_FPHP, 0)                  \
  LINE(AARCH64_ASIMDHP, asimdhp, "asimdhp", AARCH64_HWCAP_ASIMDHP, 0)      \
  LINE(AARCH64_CPUID, cpuid, "cpuid", AARCH64_HWCAP_CPUID, 0)              \
  LINE(AARCH64_ASIMDRDM, asimdrdm, "asimdrdm", AARCH64_HWCAP_ASIMDRDM, 0)  \
  LINE(AARCH64_JSCVT, jscvt, "jscvt", AARCH64_HWCAP_JSCVT, 0)              \
  LINE(AARCH64_FCMA, fcma, "fcma", AARCH64_HWCAP_FCMA, 0)                  \
  LINE(AARCH64_LRCPC, lrcpc, "lrcpc", AARCH64_HWCAP_LRCPC, 0)              \
  LINE(AARCH64_DCPOP, dcpop, "dcpop", AARCH64_HWCAP_DCPOP, 0)              \
  LINE(AARCH64_SHA3, sha3, "sha3", AARCH64_HWCAP_SHA3, 0)                  \
  LINE(AARCH64_SM3, sm3, "sm3", AARCH64_HWCAP_SM3, 0)                      \
  LINE(AARCH64_SM4, sm4, "sm4", AARCH64_HWCAP_SM4, 0)                      \
  LINE(AARCH64_ASIMDDP, asimddp, "asimddp", AARCH64_HWCAP_ASIMDDP, 0)      \
  LINE(AARCH64_SHA512, sha512, "sha512", AARCH64_HWCAP_SHA512, 0)          \
  LINE(AARCH64_SVE, sve, "sve", AARCH64_HWCAP_SVE, 0)                      \
  LINE(AARCH64_ASIMDFHM, asimdfhm, "asimdfhm", AARCH64_HWCAP_ASIMDFHM, 0)  \
  LINE(AARCH64_DIT, dit, "dit", AARCH64_HWCAP_DIT, 0)                      \
  LINE(AARCH64_USCAT, uscat, "uscat", AARCH64_HWCAP_USCAT, 0)              \
  LINE(AARCH64_ILRCPC, ilrcpc, "ilrcpc", AARCH64_HWCAP_ILRCPC, 0)          \
  LINE(AARCH64_FLAGM, flagm, "flagm", AARCH64_HWCAP_FLAGM, 0)              \
  LINE(AARCH64_SSBS, ssbs, "ssbs", AARCH64_HWCAP_SSBS, 0)                  \
  LINE(AARCH64_SB, sb, "sb", AARCH64_HWCAP_SB, 0)                          \
  LINE(AARCH64_PACA, paca, "paca", AARCH64_HWCAP_PACA, 0)                  \
  LINE(AARCH64_PACG, pacg, "pacg", AARCH64_HWCAP_PACG, 0)                  \
  LINE(AARCH64_DCPODP, dcpodp, "dcpodp", 0, AARCH64_HWCAP2_DCPODP)         \
  LINE(AARCH64_SVE2, sve2, "sve2", 0, AARCH64_HWCAP2_SVE2)                 \
  LINE(AARCH64_SVEAES, sveaes, "sveaes", 0, AARCH64_HWCAP2_SVEAES)         \
  LINE(AARCH64_SVEPMULL, svepmull, "svepmull", 0, AARCH64_HWCAP2_SVEPMULL) \
  LINE(AARCH64_SVEBITPERM, svebitperm, "svebitperm", 0,                    \
       AARCH64_HWCAP2_SVEBITPERM)                                          \
  LINE(AARCH64_SVESHA3, svesha3, "svesha3", 0, AARCH64_HWCAP2_SVESHA3)     \
  LINE(AARCH64_SVESM4, svesm4, "svesm4", 0, AARCH64_HWCAP2_SVESM4)         \
  LINE(AARCH64_FLAGM2, flagm2, "flagm2", 0, AARCH64_HWCAP2_FLAGM2)         \
  LINE(AARCH64_FRINT, frint, "frint", 0, AARCH64_HWCAP2_FRINT)             \
  LINE(AARCH64_SVEI8MM, svei8mm, "svei8mm", 0, AARCH64_HWCAP2_SVEI8MM)     \
  LINE(AARCH64_SVEF32MM, svef32mm, "svef32mm", 0, AARCH64_HWCAP2_SVEF32MM) \
  LINE(AARCH64_SVEF64MM, svef64mm, "svef64mm", 0, AARCH64_HWCAP2_SVEF64MM) \
  LINE(AARCH64_SVEBF16, svebf16, "svebf16", 0, AARCH64_HWCAP2_SVEBF16)     \
  LINE(AARCH64_I8MM, i8mm, "i8mm", 0, AARCH64_HWCAP2_I8MM)                 \
  LINE(AARCH64_BF16, bf16, "bf16", 0, AARCH64_HWCAP2_BF16)                 \
  LINE(AARCH64_DGH, dgh, "dgh", 0, AARCH64_HWCAP2_DGH)                     \
  LINE(AARCH64_RNG, rng, "rng", 0, AARCH64_HWCAP2_RNG)                     \
  LINE(AARCH64_BTI, bti, "bti", 0, AARCH64_HWCAP2_BTI)                     \
  LINE(AARCH64_MTE, mte, "mte", 0, AARCH64_HWCAP2_MTE)                     \
  LINE(AARCH64_ECV, ecv, "ecv", 0, AARCH64_HWCAP2_ECV)                     \
  LINE(AARCH64_AFP, afp, "afp", 0, AARCH64_HWCAP2_AFP)                     \
  LINE(AARCH64_RPRES, rpres, "rpres", 0, AARCH64_HWCAP2_RPRES)
#define INTROSPECTION_PREFIX Aarch64
#define INTROSPECTION_ENUM_PREFIX AARCH64
#include "define_introspection_and_hwcaps.inl"

////////////////////////////////////////////////////////////////////////////////
// Implementation.
////////////////////////////////////////////////////////////////////////////////

#include <stdbool.h>

#include "internal/bit_utils.h"
#include "internal/filesystem.h"
#include "internal/stack_line_reader.h"
#include "internal/string_view.h"

static bool HandleAarch64Line(const LineResult result,
                              Aarch64Info* const info) {
  StringView line = result.line;
  StringView key, value;
  if (CpuFeatures_StringView_GetAttributeKeyValue(line, &key, &value)) {
    if (CpuFeatures_StringView_IsEquals(key, str("Features"))) {
      for (size_t i = 0; i < AARCH64_LAST_; ++i) {
        kSetters[i](&info->features, CpuFeatures_StringView_HasWord(
                                         value, kCpuInfoFlags[i], ' '));
      }
    } else if (CpuFeatures_StringView_IsEquals(key, str("CPU implementer"))) {
      info->implementer = CpuFeatures_StringView_ParsePositiveNumber(value);
    } else if (CpuFeatures_StringView_IsEquals(key, str("CPU variant"))) {
      info->variant = CpuFeatures_StringView_ParsePositiveNumber(value);
    } else if (CpuFeatures_StringView_IsEquals(key, str("CPU part"))) {
      info->part = CpuFeatures_StringView_ParsePositiveNumber(value);
    } else if (CpuFeatures_StringView_IsEquals(key, str("CPU revision"))) {
      info->revision = CpuFeatures_StringView_ParsePositiveNumber(value);
    }
  }
  return !result.eof;
}

static void FillProcCpuInfoData(Aarch64Info* const info) {
  const int fd = CpuFeatures_OpenFile("/proc/cpuinfo");
  if (fd >= 0) {
    StackLineReader reader;
    StackLineReader_Initialize(&reader, fd);
    for (;;) {
      if (!HandleAarch64Line(StackLineReader_NextLine(&reader), info)) {
        break;
      }
    }
    CpuFeatures_CloseFile(fd);
  }
}

static const Aarch64Info kEmptyAarch64Info;

Aarch64Info GetAarch64Info(void) {
  // capabilities are fetched from both getauxval and /proc/cpuinfo so we can
  // have some information if the executable is sandboxed (aka no access to
  // /proc/cpuinfo).
  Aarch64Info info = kEmptyAarch64Info;

  FillProcCpuInfoData(&info);
  const HardwareCapabilities hwcaps = CpuFeatures_GetHardwareCapabilities();
  for (size_t i = 0; i < AARCH64_LAST_; ++i) {
    if (CpuFeatures_IsHwCapsSet(kHardwareCapabilities[i], hwcaps)) {
      kSetters[i](&info.features, true);
    }
  }

  return info;
}

#endif  // defined(CPU_FEATURES_OS_LINUX) || defined(CPU_FEATURES_OS_ANDROID)
#endif  // CPU_FEATURES_ARCH_AARCH64
