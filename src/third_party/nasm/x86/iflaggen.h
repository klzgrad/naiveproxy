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
#define IF_ANYSIZE           11 /* Ignore operand size even if explicit                             */
#define IF_AR0               12 /* SB, SW, SD applies to argument 0                                 */
#define IF_AR1               13 /* SB, SW, SD applies to argument 1                                 */
#define IF_AR2               14 /* SB, SW, SD applies to argument 2                                 */
#define IF_AR3               15 /* SB, SW, SD applies to argument 3                                 */
#define IF_AR4               16 /* SB, SW, SD applies to argument 4                                 */
#define IF_OPT               17 /* Optimizing assembly only                                         */
                                /* 17...31 reserved                                                 */
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
#define IF_CET               86 /* Intel Control-Flow Enforcement Technology (CET)                  */
#define IF_OBSOLETE          87 /* Instruction removed from architecture                            */
#define IF_NEVER             88 /* Instruction never implemented                                    */
#define IF_NOP               89 /* Instruction is always a (nonintentional) NOP                     */
#define IF_VEX               90 /* VEX or XOP encoded instruction                                   */
#define IF_EVEX              91 /* EVEX encoded instruction                                         */
                                /* 91...95 reserved                                                 */
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
#define IF_FUTURE           110 /* Ivy Bridge or newer                                              */
#define IF_IA64             111 /* IA64 (in x86 mode)                                               */
#define IF_ANY              112 /* Any x86 CPU                                                      */
#define IF_CYRIX            113 /* Cyrix-specific                                                   */
#define IF_AMD              114 /* AMD-specific                                                     */
                                /* 114...127 reserved                                               */

/* Mask bits for field 0 : 0...31 */
#define IFM_SM              UINT32_C(0x00000001)     /*   0 */
#define IFM_SM2             UINT32_C(0x00000002)     /*   1 */
#define IFM_SB              UINT32_C(0x00000004)     /*   2 */
#define IFM_SW              UINT32_C(0x00000008)     /*   3 */
#define IFM_SD              UINT32_C(0x00000010)     /*   4 */
#define IFM_SQ              UINT32_C(0x00000020)     /*   5 */
#define IFM_SO              UINT32_C(0x00000040)     /*   6 */
#define IFM_SY              UINT32_C(0x00000080)     /*   7 */
#define IFM_SZ              UINT32_C(0x00000100)     /*   8 */
#define IFM_SIZE            UINT32_C(0x00000200)     /*   9 */
#define IFM_SX              UINT32_C(0x00000400)     /*  10 */
#define IFM_ANYSIZE         UINT32_C(0x00000800)     /*  11 */
#define IFM_AR0             UINT32_C(0x00001000)     /*  12 */
#define IFM_AR1             UINT32_C(0x00002000)     /*  13 */
#define IFM_AR2             UINT32_C(0x00004000)     /*  14 */
#define IFM_AR3             UINT32_C(0x00008000)     /*  15 */
#define IFM_AR4             UINT32_C(0x00010000)     /*  16 */
#define IFM_OPT             UINT32_C(0x00020000)     /*  17 */
/* Mask bits for field 1 : 32...63 */
#define IFM_PRIV            UINT32_C(0x00000001)     /*  32 */
#define IFM_SMM             UINT32_C(0x00000002)     /*  33 */
#define IFM_PROT            UINT32_C(0x00000004)     /*  34 */
#define IFM_LOCK            UINT32_C(0x00000008)     /*  35 */
#define IFM_NOLONG          UINT32_C(0x00000010)     /*  36 */
#define IFM_LONG            UINT32_C(0x00000020)     /*  37 */
#define IFM_NOHLE           UINT32_C(0x00000040)     /*  38 */
#define IFM_MIB             UINT32_C(0x00000080)     /*  39 */
#define IFM_BND             UINT32_C(0x00000100)     /*  40 */
#define IFM_UNDOC           UINT32_C(0x00000200)     /*  41 */
#define IFM_HLE             UINT32_C(0x00000400)     /*  42 */
#define IFM_FPU             UINT32_C(0x00000800)     /*  43 */
#define IFM_MMX             UINT32_C(0x00001000)     /*  44 */
#define IFM_3DNOW           UINT32_C(0x00002000)     /*  45 */
#define IFM_SSE             UINT32_C(0x00004000)     /*  46 */
#define IFM_SSE2            UINT32_C(0x00008000)     /*  47 */
#define IFM_SSE3            UINT32_C(0x00010000)     /*  48 */
#define IFM_VMX             UINT32_C(0x00020000)     /*  49 */
#define IFM_SSSE3           UINT32_C(0x00040000)     /*  50 */
#define IFM_SSE4A           UINT32_C(0x00080000)     /*  51 */
#define IFM_SSE41           UINT32_C(0x00100000)     /*  52 */
#define IFM_SSE42           UINT32_C(0x00200000)     /*  53 */
#define IFM_SSE5            UINT32_C(0x00400000)     /*  54 */
#define IFM_AVX             UINT32_C(0x00800000)     /*  55 */
#define IFM_AVX2            UINT32_C(0x01000000)     /*  56 */
#define IFM_FMA             UINT32_C(0x02000000)     /*  57 */
#define IFM_BMI1            UINT32_C(0x04000000)     /*  58 */
#define IFM_BMI2            UINT32_C(0x08000000)     /*  59 */
#define IFM_TBM             UINT32_C(0x10000000)     /*  60 */
#define IFM_RTM             UINT32_C(0x20000000)     /*  61 */
#define IFM_INVPCID         UINT32_C(0x40000000)     /*  62 */
#define IFM_AVX512          UINT32_C(0x80000000)     /*  63 */
/* Mask bits for field 2 : 64...95 */
#define IFM_AVX512CD        UINT32_C(0x00000001)     /*  64 */
#define IFM_AVX512ER        UINT32_C(0x00000002)     /*  65 */
#define IFM_AVX512PF        UINT32_C(0x00000004)     /*  66 */
#define IFM_MPX             UINT32_C(0x00000008)     /*  67 */
#define IFM_SHA             UINT32_C(0x00000010)     /*  68 */
#define IFM_PREFETCHWT1     UINT32_C(0x00000020)     /*  69 */
#define IFM_AVX512VL        UINT32_C(0x00000040)     /*  70 */
#define IFM_AVX512DQ        UINT32_C(0x00000080)     /*  71 */
#define IFM_AVX512BW        UINT32_C(0x00000100)     /*  72 */
#define IFM_AVX512IFMA      UINT32_C(0x00000200)     /*  73 */
#define IFM_AVX512VBMI      UINT32_C(0x00000400)     /*  74 */
#define IFM_AES             UINT32_C(0x00000800)     /*  75 */
#define IFM_VAES            UINT32_C(0x00001000)     /*  76 */
#define IFM_VPCLMULQDQ      UINT32_C(0x00002000)     /*  77 */
#define IFM_GFNI            UINT32_C(0x00004000)     /*  78 */
#define IFM_AVX512VBMI2     UINT32_C(0x00008000)     /*  79 */
#define IFM_AVX512VNNI      UINT32_C(0x00010000)     /*  80 */
#define IFM_AVX512BITALG    UINT32_C(0x00020000)     /*  81 */
#define IFM_AVX512VPOPCNTDQ UINT32_C(0x00040000)     /*  82 */
#define IFM_AVX5124FMAPS    UINT32_C(0x00080000)     /*  83 */
#define IFM_AVX5124VNNIW    UINT32_C(0x00100000)     /*  84 */
#define IFM_SGX             UINT32_C(0x00200000)     /*  85 */
#define IFM_CET             UINT32_C(0x00400000)     /*  86 */
#define IFM_OBSOLETE        UINT32_C(0x00800000)     /*  87 */
#define IFM_NEVER           UINT32_C(0x01000000)     /*  88 */
#define IFM_NOP             UINT32_C(0x02000000)     /*  89 */
#define IFM_VEX             UINT32_C(0x04000000)     /*  90 */
#define IFM_EVEX            UINT32_C(0x08000000)     /*  91 */
/* Mask bits for field 3 : 96...127 */
#define IFM_8086            UINT32_C(0x00000001)     /*  96 */
#define IFM_186             UINT32_C(0x00000002)     /*  97 */
#define IFM_286             UINT32_C(0x00000004)     /*  98 */
#define IFM_386             UINT32_C(0x00000008)     /*  99 */
#define IFM_486             UINT32_C(0x00000010)     /* 100 */
#define IFM_PENT            UINT32_C(0x00000020)     /* 101 */
#define IFM_P6              UINT32_C(0x00000040)     /* 102 */
#define IFM_KATMAI          UINT32_C(0x00000080)     /* 103 */
#define IFM_WILLAMETTE      UINT32_C(0x00000100)     /* 104 */
#define IFM_PRESCOTT        UINT32_C(0x00000200)     /* 105 */
#define IFM_X86_64          UINT32_C(0x00000400)     /* 106 */
#define IFM_NEHALEM         UINT32_C(0x00000800)     /* 107 */
#define IFM_WESTMERE        UINT32_C(0x00001000)     /* 108 */
#define IFM_SANDYBRIDGE     UINT32_C(0x00002000)     /* 109 */
#define IFM_FUTURE          UINT32_C(0x00004000)     /* 110 */
#define IFM_IA64            UINT32_C(0x00008000)     /* 111 */
#define IFM_ANY             UINT32_C(0x00010000)     /* 112 */
#define IFM_CYRIX           UINT32_C(0x00020000)     /* 113 */
#define IFM_AMD             UINT32_C(0x00040000)     /* 114 */

#define IF_IGEN_FIELD         0 /* IF_SM (0) ... IF_OPT (17)                                        */
#define IF_IGEN_NFIELDS       1
#define IF_FEATURE_FIELD      1 /* IF_PRIV (32) ... IF_EVEX (91)                                    */
#define IF_FEATURE_NFIELDS    2
#define IF_CPU_FIELD          3 /* IF_8086 (96) ... IF_AMD (114)                                    */
#define IF_CPU_NFIELDS        1

#define IF_FIELD_COUNT 4
typedef struct {
    uint32_t field[IF_FIELD_COUNT];
} iflag_t;

/* All combinations of instruction flags used in instruction patterns */
extern const iflag_t insns_flags[275];

#endif /* NASM_IFLAGGEN_H */
