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

// Interface to retrieve hardware capabilities. It relies on Linux's getauxval
// or `/proc/self/auxval` under the hood.
#ifndef CPU_FEATURES_INCLUDE_INTERNAL_HWCAPS_H_
#define CPU_FEATURES_INCLUDE_INTERNAL_HWCAPS_H_

#include <stdbool.h>
#include <stdint.h>

#include "cpu_features_macros.h"

CPU_FEATURES_START_CPP_NAMESPACE

// To avoid depending on the linux kernel we reproduce the architecture specific
// constants here.

// http://elixir.free-electrons.com/linux/latest/source/arch/arm64/include/uapi/asm/hwcap.h
#define AARCH64_HWCAP_FP (UINT64_C(1) << 0)
#define AARCH64_HWCAP_ASIMD (UINT64_C(1) << 1)
#define AARCH64_HWCAP_EVTSTRM (UINT64_C(1) << 2)
#define AARCH64_HWCAP_AES (UINT64_C(1) << 3)
#define AARCH64_HWCAP_PMULL (UINT64_C(1) << 4)
#define AARCH64_HWCAP_SHA1 (UINT64_C(1) << 5)
#define AARCH64_HWCAP_SHA2 (UINT64_C(1) << 6)
#define AARCH64_HWCAP_CRC32 (UINT64_C(1) << 7)
#define AARCH64_HWCAP_ATOMICS (UINT64_C(1) << 8)
#define AARCH64_HWCAP_FPHP (UINT64_C(1) << 9)
#define AARCH64_HWCAP_ASIMDHP (UINT64_C(1) << 10)
#define AARCH64_HWCAP_CPUID (UINT64_C(1) << 11)
#define AARCH64_HWCAP_ASIMDRDM (UINT64_C(1) << 12)
#define AARCH64_HWCAP_JSCVT (UINT64_C(1) << 13)
#define AARCH64_HWCAP_FCMA (UINT64_C(1) << 14)
#define AARCH64_HWCAP_LRCPC (UINT64_C(1) << 15)
#define AARCH64_HWCAP_DCPOP (UINT64_C(1) << 16)
#define AARCH64_HWCAP_SHA3 (UINT64_C(1) << 17)
#define AARCH64_HWCAP_SM3 (UINT64_C(1) << 18)
#define AARCH64_HWCAP_SM4 (UINT64_C(1) << 19)
#define AARCH64_HWCAP_ASIMDDP (UINT64_C(1) << 20)
#define AARCH64_HWCAP_SHA512 (UINT64_C(1) << 21)
#define AARCH64_HWCAP_SVE (UINT64_C(1) << 22)
#define AARCH64_HWCAP_ASIMDFHM (UINT64_C(1) << 23)
#define AARCH64_HWCAP_DIT (UINT64_C(1) << 24)
#define AARCH64_HWCAP_USCAT (UINT64_C(1) << 25)
#define AARCH64_HWCAP_ILRCPC (UINT64_C(1) << 26)
#define AARCH64_HWCAP_FLAGM (UINT64_C(1) << 27)
#define AARCH64_HWCAP_SSBS (UINT64_C(1) << 28)
#define AARCH64_HWCAP_SB (UINT64_C(1) << 29)
#define AARCH64_HWCAP_PACA (UINT64_C(1) << 30)
#define AARCH64_HWCAP_PACG (UINT64_C(1) << 31)

#define AARCH64_HWCAP2_DCPODP (UINT64_C(1) << 0)
#define AARCH64_HWCAP2_SVE2 (UINT64_C(1) << 1)
#define AARCH64_HWCAP2_SVEAES (UINT64_C(1) << 2)
#define AARCH64_HWCAP2_SVEPMULL (UINT64_C(1) << 3)
#define AARCH64_HWCAP2_SVEBITPERM (UINT64_C(1) << 4)
#define AARCH64_HWCAP2_SVESHA3 (UINT64_C(1) << 5)
#define AARCH64_HWCAP2_SVESM4 (UINT64_C(1) << 6)
#define AARCH64_HWCAP2_FLAGM2 (UINT64_C(1) << 7)
#define AARCH64_HWCAP2_FRINT (UINT64_C(1) << 8)
#define AARCH64_HWCAP2_SVEI8MM (UINT64_C(1) << 9)
#define AARCH64_HWCAP2_SVEF32MM (UINT64_C(1) << 10)
#define AARCH64_HWCAP2_SVEF64MM (UINT64_C(1) << 11)
#define AARCH64_HWCAP2_SVEBF16 (UINT64_C(1) << 12)
#define AARCH64_HWCAP2_I8MM (UINT64_C(1) << 13)
#define AARCH64_HWCAP2_BF16 (UINT64_C(1) << 14)
#define AARCH64_HWCAP2_DGH (UINT64_C(1) << 15)
#define AARCH64_HWCAP2_RNG (UINT64_C(1) << 16)
#define AARCH64_HWCAP2_BTI (UINT64_C(1) << 17)
#define AARCH64_HWCAP2_MTE (UINT64_C(1) << 18)
#define AARCH64_HWCAP2_ECV (UINT64_C(1) << 19)
#define AARCH64_HWCAP2_AFP (UINT64_C(1) << 20)
#define AARCH64_HWCAP2_RPRES (UINT64_C(1) << 21)
#define AARCH64_HWCAP2_MTE3 (UINT64_C(1) << 22)
#define AARCH64_HWCAP2_SME (UINT64_C(1) << 23)
#define AARCH64_HWCAP2_SME_I16I64 (UINT64_C(1) << 24)
#define AARCH64_HWCAP2_SME_F64F64 (UINT64_C(1) << 25)
#define AARCH64_HWCAP2_SME_I8I32 (UINT64_C(1) << 26)
#define AARCH64_HWCAP2_SME_F16F32 (UINT64_C(1) << 27)
#define AARCH64_HWCAP2_SME_B16F32 (UINT64_C(1) << 28)
#define AARCH64_HWCAP2_SME_F32F32 (UINT64_C(1) << 29)
#define AARCH64_HWCAP2_SME_FA64 (UINT64_C(1) << 30)
#define AARCH64_HWCAP2_WFXT (UINT64_C(1) << 31)
#define AARCH64_HWCAP2_EBF16 (UINT64_C(1) << 32)
#define AARCH64_HWCAP2_SVE_EBF16 (UINT64_C(1) << 33)
#define AARCH64_HWCAP2_CSSC (UINT64_C(1) << 34)
#define AARCH64_HWCAP2_RPRFM (UINT64_C(1) << 35)
#define AARCH64_HWCAP2_SVE2P1 (UINT64_C(1) << 36)
#define AARCH64_HWCAP2_SME2 (UINT64_C(1) << 37)
#define AARCH64_HWCAP2_SME2P1 (UINT64_C(1) << 38)
#define AARCH64_HWCAP2_SME_I16I32 (UINT64_C(1) << 39)
#define AARCH64_HWCAP2_SME_BI32I32 (UINT64_C(1) << 40)
#define AARCH64_HWCAP2_SME_B16B16 (UINT64_C(1) << 41)
#define AARCH64_HWCAP2_SME_F16F16 (UINT64_C(1) << 42)
#define AARCH64_HWCAP2_MOPS (UINT64_C(1) << 43)
#define AARCH64_HWCAP2_HBC (UINT64_C(1) << 44)
#define AARCH64_HWCAP2_SVE_B16B16 (UINT64_C(1) << 45)
#define AARCH64_HWCAP2_LRCPC3 (UINT64_C(1) << 46)
#define AARCH64_HWCAP2_LSE128 (UINT64_C(1) << 47)
#define AARCH64_HWCAP2_FPMR (UINT64_C(1) << 48)
#define AARCH64_HWCAP2_LUT (UINT64_C(1) << 49)
#define AARCH64_HWCAP2_FAMINMAX (UINT64_C(1) << 50)
#define AARCH64_HWCAP2_F8CVT (UINT64_C(1) << 51)
#define AARCH64_HWCAP2_F8FMA (UINT64_C(1) << 52)
#define AARCH64_HWCAP2_F8DP4 (UINT64_C(1) << 53)
#define AARCH64_HWCAP2_F8DP2 (UINT64_C(1) << 54)
#define AARCH64_HWCAP2_F8E4M3 (UINT64_C(1) << 55)
#define AARCH64_HWCAP2_F8E5M2 (UINT64_C(1) << 56)
#define AARCH64_HWCAP2_SME_LUTV2 (UINT64_C(1) << 57)
#define AARCH64_HWCAP2_SME_F8F16 (UINT64_C(1) << 58)
#define AARCH64_HWCAP2_SME_F8F32 (UINT64_C(1) << 59)
#define AARCH64_HWCAP2_SME_SF8FMA (UINT64_C(1) << 60)
#define AARCH64_HWCAP2_SME_SF8DP4 (UINT64_C(1) << 61)
#define AARCH64_HWCAP2_SME_SF8DP2 (UINT64_C(1) << 62)
#define AARCH64_HWCAP2_POE (UINT64_C(1) << 63)

// http://elixir.free-electrons.com/linux/latest/source/arch/arm/include/uapi/asm/hwcap.h
#define ARM_HWCAP_SWP (UINT64_C(1) << 0)
#define ARM_HWCAP_HALF (UINT64_C(1) << 1)
#define ARM_HWCAP_THUMB (UINT64_C(1) << 2)
#define ARM_HWCAP_26BIT (UINT64_C(1) << 3)
#define ARM_HWCAP_FAST_MULT (UINT64_C(1) << 4)
#define ARM_HWCAP_FPA (UINT64_C(1) << 5)
#define ARM_HWCAP_VFP (UINT64_C(1) << 6)
#define ARM_HWCAP_EDSP (UINT64_C(1) << 7)
#define ARM_HWCAP_JAVA (UINT64_C(1) << 8)
#define ARM_HWCAP_IWMMXT (UINT64_C(1) << 9)
#define ARM_HWCAP_CRUNCH (UINT64_C(1) << 10)
#define ARM_HWCAP_THUMBEE (UINT64_C(1) << 11)
#define ARM_HWCAP_NEON (UINT64_C(1) << 12)
#define ARM_HWCAP_VFPV3 (UINT64_C(1) << 13)
#define ARM_HWCAP_VFPV3D16 (UINT64_C(1) << 14)
#define ARM_HWCAP_TLS (UINT64_C(1) << 15)
#define ARM_HWCAP_VFPV4 (UINT64_C(1) << 16)
#define ARM_HWCAP_IDIVA (UINT64_C(1) << 17)
#define ARM_HWCAP_IDIVT (UINT64_C(1) << 18)
#define ARM_HWCAP_VFPD32 (UINT64_C(1) << 19)
#define ARM_HWCAP_LPAE (UINT64_C(1) << 20)
#define ARM_HWCAP_EVTSTRM (UINT64_C(1) << 21)
#define ARM_HWCAP2_AES (UINT64_C(1) << 0)
#define ARM_HWCAP2_PMULL (UINT64_C(1) << 1)
#define ARM_HWCAP2_SHA1 (UINT64_C(1) << 2)
#define ARM_HWCAP2_SHA2 (UINT64_C(1) << 3)
#define ARM_HWCAP2_CRC32 (UINT64_C(1) << 4)

// http://elixir.free-electrons.com/linux/latest/source/arch/mips/include/uapi/asm/hwcap.h
#define MIPS_HWCAP_R6 (UINT64_C(1) << 0)
#define MIPS_HWCAP_MSA (UINT64_C(1) << 1)
#define MIPS_HWCAP_CRC32 (UINT64_C(1) << 2)
#define MIPS_HWCAP_MIPS16 (UINT64_C(1) << 3)
#define MIPS_HWCAP_MDMX (UINT64_C(1) << 4)
#define MIPS_HWCAP_MIPS3D (UINT64_C(1) << 5)
#define MIPS_HWCAP_SMARTMIPS (UINT64_C(1) << 6)
#define MIPS_HWCAP_DSP (UINT64_C(1) << 7)
#define MIPS_HWCAP_DSP2 (UINT64_C(1) << 8)
#define MIPS_HWCAP_DSP3 (UINT64_C(1) << 9)

// http://elixir.free-electrons.com/linux/latest/source/arch/powerpc/include/uapi/asm/cputable.h
#ifndef _UAPI__ASM_POWERPC_CPUTABLE_H
/* in AT_HWCAP */
#define PPC_FEATURE_32 UINT64_C(0x80000000)
#define PPC_FEATURE_64 UINT64_C(0x40000000)
#define PPC_FEATURE_601_INSTR UINT64_C(0x20000000)
#define PPC_FEATURE_HAS_ALTIVEC UINT64_C(0x10000000)
#define PPC_FEATURE_HAS_FPU UINT64_C(0x08000000)
#define PPC_FEATURE_HAS_MMU UINT64_C(0x04000000)
#define PPC_FEATURE_HAS_4xxMAC UINT64_C(0x02000000)
#define PPC_FEATURE_UNIFIED_CACHE UINT64_C(0x01000000)
#define PPC_FEATURE_HAS_SPE UINT64_C(0x00800000)
#define PPC_FEATURE_HAS_EFP_SINGLE UINT64_C(0x00400000)
#define PPC_FEATURE_HAS_EFP_DOUBLE UINT64_C(0x00200000)
#define PPC_FEATURE_NO_TB UINT64_C(0x00100000)
#define PPC_FEATURE_POWER4 UINT64_C(0x00080000)
#define PPC_FEATURE_POWER5 UINT64_C(0x00040000)
#define PPC_FEATURE_POWER5_PLUS UINT64_C(0x00020000)
#define PPC_FEATURE_CELL UINT64_C(0x00010000)
#define PPC_FEATURE_BOOKE UINT64_C(0x00008000)
#define PPC_FEATURE_SMT UINT64_C(0x00004000)
#define PPC_FEATURE_ICACHE_SNOOP UINT64_C(0x00002000)
#define PPC_FEATURE_ARCH_2_05 UINT64_C(0x00001000)
#define PPC_FEATURE_PA6T UINT64_C(0x00000800)
#define PPC_FEATURE_HAS_DFP UINT64_C(0x00000400)
#define PPC_FEATURE_POWER6_EXT UINT64_C(0x00000200)
#define PPC_FEATURE_ARCH_2_06 UINT64_C(0x00000100)
#define PPC_FEATURE_HAS_VSX UINT64_C(0x00000080)

#define PPC_FEATURE_PSERIES_PERFMON_COMPAT UINT64_C(0x00000040)

/* Reserved - do not use                0x00000004 */
#define PPC_FEATURE_TRUE_LE UINT64_C(0x00000002)
#define PPC_FEATURE_PPC_LE UINT64_C(0x00000001)

/* in AT_HWCAP2 */
#define PPC_FEATURE2_ARCH_2_07 UINT64_C(0x80000000)
#define PPC_FEATURE2_HTM UINT64_C(0x40000000)
#define PPC_FEATURE2_DSCR UINT64_C(0x20000000)
#define PPC_FEATURE2_EBB UINT64_C(0x10000000)
#define PPC_FEATURE2_ISEL UINT64_C(0x08000000)
#define PPC_FEATURE2_TAR UINT64_C(0x04000000)
#define PPC_FEATURE2_VEC_CRYPTO UINT64_C(0x02000000)
#define PPC_FEATURE2_HTM_NOSC UINT64_C(0x01000000)
#define PPC_FEATURE2_ARCH_3_00 UINT64_C(0x00800000)
#define PPC_FEATURE2_HAS_IEEE128 UINT64_C(0x00400000)
#define PPC_FEATURE2_DARN UINT64_C(0x00200000)
#define PPC_FEATURE2_SCV UINT64_C(0x00100000)
#define PPC_FEATURE2_HTM_NO_SUSPEND UINT64_C(0x00080000)
#endif

// https://elixir.bootlin.com/linux/v6.0-rc6/source/arch/s390/include/asm/elf.h
#define HWCAP_S390_ESAN3 UINT64_C(1)
#define HWCAP_S390_ZARCH UINT64_C(2)
#define HWCAP_S390_STFLE UINT64_C(4)
#define HWCAP_S390_MSA UINT64_C(8)
#define HWCAP_S390_LDISP UINT64_C(16)
#define HWCAP_S390_EIMM UINT64_C(32)
#define HWCAP_S390_DFP UINT64_C(64)
#define HWCAP_S390_HPAGE UINT64_C(128)
#define HWCAP_S390_ETF3EH UINT64_C(256)
#define HWCAP_S390_HIGH_GPRS UINT64_C(512)
#define HWCAP_S390_TE UINT64_C(1024)
#define HWCAP_S390_VX UINT64_C(2048)
#define HWCAP_S390_VXRS HWCAP_S390_VX
#define HWCAP_S390_VXD UINT64_C(4096)
#define HWCAP_S390_VXRS_BCD HWCAP_S390_VXD
#define HWCAP_S390_VXE UINT64_C(8192)
#define HWCAP_S390_VXRS_EXT HWCAP_S390_VXE
#define HWCAP_S390_GS UINT64_C(16384)
#define HWCAP_S390_VXRS_EXT2 UINT64_C(32768)
#define HWCAP_S390_VXRS_PDE UINT64_C(65536)
#define HWCAP_S390_SORT UINT64_C(131072)
#define HWCAP_S390_DFLT UINT64_C(262144)
#define HWCAP_S390_VXRS_PDE2 UINT64_C(524288)
#define HWCAP_S390_NNPA UINT64_C(1048576)
#define HWCAP_S390_PCI_MIO UINT64_C(2097152)
#define HWCAP_S390_SIE UINT64_C(4194304)

// https://elixir.bootlin.com/linux/latest/source/arch/riscv/include/uapi/asm/hwcap.h
#define RISCV_HWCAP_32 UINT64_C(0x32)
#define RISCV_HWCAP_64 UINT64_C(0x64)
#define RISCV_HWCAP_128 UINT64_C(0x128)
#define RISCV_HWCAP_M (UINT64_C(1) << ('M' - 'A'))
#define RISCV_HWCAP_A (UINT64_C(1) << ('A' - 'A'))
#define RISCV_HWCAP_F (UINT64_C(1) << ('F' - 'A'))
#define RISCV_HWCAP_D (UINT64_C(1) << ('D' - 'A'))
#define RISCV_HWCAP_Q (UINT64_C(1) << ('Q' - 'A'))
#define RISCV_HWCAP_C (UINT64_C(1) << ('C' - 'A'))
#define RISCV_HWCAP_V (UINT64_C(1) << ('V' - 'A'))

// https://github.com/torvalds/linux/blob/master/arch/loongarch/include/uapi/asm/hwcap.h
#define HWCAP_LOONGARCH_CPUCFG (UINT64_C(1) << 0)
#define HWCAP_LOONGARCH_LAM (UINT64_C(1) << 1)
#define HWCAP_LOONGARCH_UAL (UINT64_C(1) << 2)
#define HWCAP_LOONGARCH_FPU (UINT64_C(1) << 3)
#define HWCAP_LOONGARCH_LSX (UINT64_C(1) << 4)
#define HWCAP_LOONGARCH_LASX (UINT64_C(1) << 5)
#define HWCAP_LOONGARCH_CRC32 (UINT64_C(1) << 6)
#define HWCAP_LOONGARCH_COMPLEX (UINT64_C(1) << 7)
#define HWCAP_LOONGARCH_CRYPTO (UINT64_C(1) << 8)
#define HWCAP_LOONGARCH_LVZ (UINT64_C(1) << 9)
#define HWCAP_LOONGARCH_LBT_X86 (UINT64_C(1) << 10)
#define HWCAP_LOONGARCH_LBT_ARM (UINT64_C(1) << 11)
#define HWCAP_LOONGARCH_LBT_MIPS (UINT64_C(1) << 12)
#define HWCAP_LOONGARCH_PTW (UINT64_C(1) << 13)

typedef struct {
  uint64_t hwcaps;
  uint64_t hwcaps2;
} HardwareCapabilities;

// Retrieves values from auxiliary vector for types AT_HWCAP and AT_HWCAP2.
// First tries to call getauxval(), if not available falls back to reading
// "/proc/self/auxv".
HardwareCapabilities CpuFeatures_GetHardwareCapabilities(void);

// Checks whether value for AT_HWCAP (or AT_HWCAP2) match hwcaps_mask.
bool CpuFeatures_IsHwCapsSet(const HardwareCapabilities hwcaps_mask,
                             const HardwareCapabilities hwcaps);

// Get pointer for the AT_PLATFORM type.
const char* CpuFeatures_GetPlatformPointer(void);
// Get pointer for the AT_BASE_PLATFORM type.
const char* CpuFeatures_GetBasePlatformPointer(void);

CPU_FEATURES_END_CPP_NAMESPACE

#endif  // CPU_FEATURES_INCLUDE_INTERNAL_HWCAPS_H_
