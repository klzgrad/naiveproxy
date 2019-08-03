/* This file is auto-generated. Don't edit. */
#ifndef NASM_IFLAGGEN_H
#define NASM_IFLAGGEN_H 1

#define IF_SM                 0 /* Size match                                                       */
#define IF_SM2                1 /* Size match first two operands                                    */
#define IF_SB                 2 /* Unsized operands can't be non-byte                               */
#define IF_SW                 3 /* Unsized operands can't be non-word                               */
#define IF_SD                 4 /* Unsized operands can't be non-dword                              */
#define IF_SQ                 5 /* Unsized operands can't be non-qword                              */
#define IF_SO                 6 /* Unsized operands can't be non-oword                              */
#define IF_SY                 7 /* Unsized operands can't be non-yword                              */
#define IF_SZ                 8 /* Unsized operands can't be non-zword                              */
#define IF_SIZE               9 /* Unsized operands must match the bitsize                          */
#define IF_SX                10 /* Unsized operands not allowed                                     */
#define IF_AR0               11 /* SB, SW, SD applies to argument 0                                 */
#define IF_AR1               12 /* SB, SW, SD applies to argument 1                                 */
#define IF_AR2               13 /* SB, SW, SD applies to argument 2                                 */
#define IF_AR3               14 /* SB, SW, SD applies to argument 3                                 */
#define IF_AR4               15 /* SB, SW, SD applies to argument 4                                 */
#define IF_OPT               16 /* Optimizing assembly only                                         */
                                /* 17...31 unused                                                   */
#define IF_PRIV              32 /* Privileged instruction                                           */
#define IF_SMM               33 /* Only valid in SMM                                                */
#define IF_PROT              34 /* Protected mode only                                              */
#define IF_LOCK              35 /* Lockable if operand 0 is memory                                  */
#define IF_NOLONG            36 /* Not available in long mode                                       */
#define IF_LONG              37 /* Long mode                                                        */
#define IF_NOHLE             38 /* HLE prefixes forbidden                                           */
#define IF_MIB               39 /* disassemble with split EA                                        */
#define IF_BND               40 /* BND (0xF2) prefix available                                      */
#define IF_UNDOC             41 /* Undocumented                                                     */
#define IF_HLE               42 /* HLE prefixed                                                     */
#define IF_FPU               43 /* FPU                                                              */
#define IF_MMX               44 /* MMX                                                              */
#define IF_3DNOW             45 /* 3DNow!                                                           */
#define IF_SSE               46 /* SSE (KNI, MMX2)                                                  */
#define IF_SSE2              47 /* SSE2                                                             */
#define IF_SSE3              48 /* SSE3 (PNI)                                                       */
#define IF_VMX               49 /* VMX                                                              */
#define IF_SSSE3             50 /* SSSE3                                                            */
#define IF_SSE4A             51 /* AMD SSE4a                                                        */
#define IF_SSE41             52 /* SSE4.1                                                           */
#define IF_SSE42             53 /* SSE4.2                                                           */
#define IF_SSE5              54 /* SSE5                                                             */
#define IF_AVX               55 /* AVX  (256-bit floating point)                                    */
#define IF_AVX2              56 /* AVX2 (256-bit integer)                                           */
#define IF_FMA               57 /*                                                                  */
#define IF_BMI1              58 /*                                                                  */
#define IF_BMI2              59 /*                                                                  */
#define IF_TBM               60 /*                                                                  */
#define IF_RTM               61 /*                                                                  */
#define IF_INVPCID           62 /*                                                                  */
#define IF_AVX512            63 /* AVX-512F (512-bit base architecture)                             */
#define IF_AVX512CD          64 /* AVX-512 Conflict Detection                                       */
#define IF_AVX512ER          65 /* AVX-512 Exponential and Reciprocal                               */
#define IF_AVX512PF          66 /* AVX-512 Prefetch                                                 */
#define IF_MPX               67 /* MPX                                                              */
#define IF_SHA               68 /* SHA                                                              */
#define IF_PREFETCHWT1       69 /* PREFETCHWT1                                                      */
#define IF_AVX512VL          70 /* AVX-512 Vector Length Orthogonality                              */
#define IF_AVX512DQ          71 /* AVX-512 Dword and Qword                                          */
#define IF_AVX512BW          72 /* AVX-512 Byte and Word                                            */
#define IF_AVX512IFMA        73 /* AVX-512 IFMA instructions                                        */
#define IF_AVX512VBMI        74 /* AVX-512 VBMI instructions                                        */
#define IF_AES               75 /* AES instructions                                                 */
#define IF_VAES              76 /* AES AVX instructions                                             */
#define IF_VPCLMULQDQ        77 /* AVX Carryless Multiplication                                     */
#define IF_GFNI              78 /* Galois Field instructions                                        */
#define IF_AVX512VBMI2       79 /* AVX-512 VBMI2 instructions                                       */
#define IF_AVX512VNNI        80 /* AVX-512 VNNI instructions                                        */
#define IF_AVX512BITALG      81 /* AVX-512 Bit Algorithm instructions                               */
#define IF_AVX512VPOPCNTDQ   82 /* AVX-512 VPOPCNTD/VPOPCNTQ                                        */
#define IF_AVX5124FMAPS      83 /* AVX-512 4-iteration multiply-add                                 */
#define IF_AVX5124VNNIW      84 /* AVX-512 4-iteration dot product                                  */
#define IF_SGX               85 /* Intel Software Guard Extensions (SGX)                            */
#define IF_OBSOLETE          86 /* Instruction removed from architecture                            */
#define IF_VEX               87 /* VEX or XOP encoded instruction                                   */
#define IF_EVEX              88 /* EVEX encoded instruction                                         */
                                /* 89...95 unused                                                   */
#define IF_8086              96 /* 8086                                                             */
#define IF_186               97 /* 186+                                                             */
#define IF_286               98 /* 286+                                                             */
#define IF_386               99 /* 386+                                                             */
#define IF_486              100 /* 486+                                                             */
#define IF_PENT             101 /* Pentium                                                          */
#define IF_P6               102 /* P6                                                               */
#define IF_KATMAI           103 /* Katmai                                                           */
#define IF_WILLAMETTE       104 /* Willamette                                                       */
#define IF_PRESCOTT         105 /* Prescott                                                         */
#define IF_X86_64           106 /* x86-64 (long or legacy mode)                                     */
#define IF_NEHALEM          107 /* Nehalem                                                          */
#define IF_WESTMERE         108 /* Westmere                                                         */
#define IF_SANDYBRIDGE      109 /* Sandy Bridge                                                     */
#define IF_FUTURE           110 /* Future processor (not yet disclosed)                             */
#define IF_IA64             111 /* IA64 (in x86 mode)                                               */
#define IF_CYRIX            112 /* Cyrix-specific                                                   */
#define IF_AMD              113 /* AMD-specific                                                     */

#define IF_FIELD_COUNT 4
typedef struct {
    uint32_t field[IF_FIELD_COUNT];
} iflag_t;

extern const iflag_t insns_flags[263];

#endif /* NASM_IFLAGGEN_H */
