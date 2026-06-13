/* This file is auto-generated. Don't edit. */
#ifndef NASM_IFLAGGEN_H
#define NASM_IFLAGGEN_H 1

#define IF_SM0                0 /* Size match operand 0                                             */
#define IF_SM1                1 /* Size match operand 1                                             */
#define IF_SM2                2 /* Size match operand 2                                             */
#define IF_SM3                3 /* Size match operand 3                                             */
#define IF_SM4                4 /* Size match operand 4                                             */
#define IF_AR0                5 /* SB, SW, SD applies to operand 0                                  */
#define IF_AR1                6 /* SB, SW, SD applies to operand 1                                  */
#define IF_AR2                7 /* SB, SW, SD applies to operand 2                                  */
#define IF_AR3                8 /* SB, SW, SD applies to operand 3                                  */
#define IF_AR4                9 /* SB, SW, SD applies to operand 4                                  */
#define IF_SB                10 /* Unsized operands can't be non-byte                               */
#define IF_SW                11 /* Unsized operands can't be non-word                               */
#define IF_SD                12 /* Unsized operands can't be non-dword                              */
#define IF_SQ                13 /* Unsized operands can't be non-qword                              */
#define IF_ST                14 /* Unsized operands can't be non-tword                              */
#define IF_SO                15 /* Unsized operands can't be non-oword                              */
#define IF_SY                16 /* Unsized operands can't be non-yword                              */
#define IF_SZ                17 /* Unsized operands can't be non-zword                              */
#define IF_NWSIZE            18 /* Operand size defaults to 64 in 64-bit mode                       */
#define IF_OSIZE             19 /* Unsized operands must match the operand size                     */
#define IF_ASIZE             20 /* Unsized operands must match the address size                     */
#define IF_ANYSIZE           21 /* Ignore operand size even if explicit                             */
#define IF_SX                22 /* Unsized operands not allowed                                     */
#define IF_SDWORD            23 /* Strict sdword64 matching                                         */
#define IF_PSEUDO            24 /* Pseudo-instruction (directive)                                   */
#define IF_JMP_RELAX         25 /* Relaxable jump instruction                                       */
#define IF_JCC_HINT          26 /* Hintable jump instruction                                        */
#define IF_OPT               27 /* Optimizing assembly only                                         */
#define IF_LATEVEX           28 /* Only if EVEX instructions are disabled                           */
#define IF_NOREX             29 /* Instruction does not support REX encoding                        */
#define IF_NOAPX             30 /* Instruction does not support APX registers or REX2               */
#define IF_NF                31 /* Instruction supports the {nf} prefix                             */
#define IF_NF_R              32 /* Instruction requires the {nf} prefix                             */
#define IF_NF_N              33 /* Instruction doesn't allow the {nf} prefix                        */
#define IF_NF_E              34 /* EVEX.NF set with {nf} prefix                                     */
#define IF_ZU                35 /* Instruction supports the {zu} prefix                             */
#define IF_ZU_R              36 /* Instruction requires the {zu} prefix                             */
#define IF_ZU_E              37 /* EVEX.ND set with {zu} prefix                                     */
#define IF_LIG               38 /* Ignore VEX/EVEX L field                                          */
#define IF_WIG               39 /* Ignore VEX/EVEX W field                                          */
#define IF_WW                40 /* VEX/EVEX W is REX.W                                              */
#define IF_SIB               41 /* SIB encoding required                                            */
#define IF_LOCK              42 /* Lockable if operand 0 is memory                                  */
#define IF_LOCK1             43 /* Lockable if operand 1 is memory                                  */
#define IF_NOLONG            44 /* Not available in long mode                                       */
#define IF_LONG              45 /* Long mode                                                        */
#define IF_NOHLE             46 /* HLE prefixes forbidden                                           */
#define IF_MIB               47 /* split base/index EA                                              */
#define IF_BND               48 /* BND (0xF2) prefix available                                      */
#define IF_REX2              49 /* REX2 encoding required                                           */
#define IF_HLE               50 /* HLE prefixed                                                     */
#define IF_FL                51 /* Instruction modifies the flags                                   */
#define IF_MOPVEC            52 /* M operand is a vector                                            */
#define IF_SCC               53 /* EVEX[27:24] is special condition code                            */
#define IF_BESTDIS           54 /* Preferred disassembly pattern                                    */
#define IF_DFV               55 /* Destination flag values                                          */
                                /* 55...63 reserved                                                 */
#define IF_VEX               64 /* VEX or XOP encoded instruction                                   */
#define IF_EVEX              65 /* EVEX encoded instruction                                         */
#define IF_PRIV              66 /* Privileged instruction                                           */
#define IF_SMM               67 /* Only valid in SMM                                                */
#define IF_PROT              68 /* Protected mode only                                              */
#define IF_UNDOC             69 /* Undocumented                                                     */
#define IF_FPU               70 /* FPU                                                              */
#define IF_MMX               71 /* MMX                                                              */
#define IF_3DNOW             72 /* 3DNow!                                                           */
#define IF_SSE               73 /* SSE (KNI, MMX2)                                                  */
#define IF_SSE2              74 /* SSE2                                                             */
#define IF_SSE3              75 /* SSE3 (PNI)                                                       */
#define IF_VMX               76 /* VMX                                                              */
#define IF_SSSE3             77 /* SSSE3                                                            */
#define IF_SSE4A             78 /* AMD SSE4a                                                        */
#define IF_SSE41             79 /* SSE4.1                                                           */
#define IF_SSE42             80 /* SSE4.2                                                           */
#define IF_SSE5              81 /* SSE5                                                             */
#define IF_AVX               82 /* AVX  (256-bit floating point)                                    */
#define IF_AVX2              83 /* AVX2 (256-bit integer)                                           */
#define IF_FMA               84 /* Fused multiply-add                                               */
#define IF_BMI1              85 /* Bit manipulation instructions 1                                  */
#define IF_BMI2              86 /* Bit manipulation instructions 2                                  */
#define IF_TBM               87 /*                                                                  */
#define IF_RTM               88 /*                                                                  */
#define IF_AVX512            89 /* AVX-512                                                          */
#define IF_AVX512F           90 /* AVX-512F (base architecture)                                     */
#define IF_AVX512CD          91 /* AVX-512 Conflict Detection                                       */
#define IF_AVX512ER          92 /* AVX-512 Exponential and Reciprocal                               */
#define IF_AVX512PF          93 /* AVX-512 Prefetch                                                 */
#define IF_MPX               94 /* MPX                                                              */
#define IF_SHA               95 /* SHA                                                              */
#define IF_AVX512VL          96 /* AVX-512 Vector Length Orthogonality                              */
#define IF_AVX512DQ          97 /* AVX-512 Dword and Qword                                          */
#define IF_AVX512BW          98 /* AVX-512 Byte and Word                                            */
#define IF_AVX512IFMA        99 /* AVX-512 IFMA instructions                                        */
#define IF_AVX512VBMI       100 /* AVX-512 VBMI instructions                                        */
#define IF_AES              101 /* AES instructions                                                 */
#define IF_VAES             102 /* AES AVX instructions                                             */
#define IF_VPCLMULQDQ       103 /* AVX Carryless Multiplication                                     */
#define IF_GFNI             104 /* Galois Field instructions                                        */
#define IF_AVX512VBMI2      105 /* AVX-512 VBMI2 instructions                                       */
#define IF_AVX512VNNI       106 /* AVX-512 VNNI instructions                                        */
#define IF_AVX512BITALG     107 /* AVX-512 Bit Algorithm instructions                               */
#define IF_AVX512VPOPCNTDQ  108 /* AVX-512 VPOPCNTD/VPOPCNTQ                                        */
#define IF_AVX5124FMAPS     109 /* AVX-512 4-iteration multiply-add                                 */
#define IF_AVX5124VNNIW     110 /* AVX-512 4-iteration dot product                                  */
#define IF_AVX512FP16       111 /* AVX-512 FP16 instructions                                        */
#define IF_F16C             112 /* F16C instructions                                                */
#define IF_SGX              113 /* Intel Software Guard Extensions (SGX)                            */
#define IF_CET              114 /* Intel Control-Flow Enforcement Technology (CET)                  */
#define IF_ENQCMD           115 /* Enqueue command instructions                                     */
#define IF_TSXLDTRK         116 /* TSX suspend load address tracking                                */
#define IF_AVX512BF16       117 /* AVX-512 bfloat16                                                 */
#define IF_AVX512VP2INTERSECT 118 /* AVX-512 VP2INTERSECT instructions                                */
#define IF_AMXTILE          119 /* AMX tile configuration instructions                              */
#define IF_AMXBF16          120 /* AMX bfloat16 multiplication                                      */
#define IF_AMXFP16          121 /* AMX FP16 multiplication                                          */
#define IF_AMXFP8           122 /* AMX FP8 instructions                                             */
#define IF_AMXTF32          123 /* AMX TF32 multiplication                                          */
#define IF_AMXINT8          124 /* AMX 8-bit integer multiplication                                 */
#define IF_AMXCOMPLEX       125 /* AMX float16 complex multiplication                               */
#define IF_AMXAVX512        126 /* EVEX zmm<-tmm conversion instructions                            */
#define IF_AMXMOVRS         127 /* AMX loads with MOVRS hint                                        */
#define IF_AMXTRANSPOSE     128 /* AMX transpose instructions                                       */
#define IF_FRED             129 /* Flexible Return and Exception Delivery (FRED)                    */
#define IF_RAOINT           130 /* Remote atomic operations (RAO-INT)                               */
#define IF_UINTR            131 /* User interrupts                                                  */
#define IF_CMPCCXADD        132 /* CMPccXADD instructions                                           */
#define IF_PREFETCHI        133 /* PREFETCHI0 and PREFETCHI1                                        */
#define IF_MSRLIST          134 /* RDMSRLIST and WRMSRLIST                                          */
#define IF_AVXNECONVERT     135 /* AVX exceptionless floating-point conversions                     */
#define IF_AVXVNNI          136 /* AVX Vector Neural Network instructions                           */
#define IF_AVXVNNIINT8      137 /* AVX Vector Neural Network 8-bit integer instructions             */
#define IF_AVXVNNIINT16     138 /* AVX Vector Neural Network 16-bit integer instructions            */
#define IF_AVXIFMA          139 /* AVX integer multiply and add                                     */
#define IF_HRESET           140 /* History reset                                                    */
#define IF_SMAP             141 /* Supervisor Mode Access Prevention (SMAP)                         */
#define IF_SHA512           142 /* SHA512 instructions                                              */
#define IF_HSM3             143 /* SM3 hash instructions                                            */
#define IF_HSM4             144 /* SM4 hash instructions                                            */
#define IF_APX              145 /* Advanced Performance Extensions (APX)                            */
#define IF_AVX10_1          146 /* AVX 10.1 instructions                                            */
#define IF_AVX10_2          147 /* AVX 10.2 instructions                                            */
#define IF_AVX10_VNNIINT    148 /* AVX Vector Neural Network integer instructions                   */
#define IF_ADX              149 /* ADCX and ADOX instructions                                       */
#define IF_PKU              150 /* Protection key for user mode                                     */
#define IF_MONITOR          151 /* MONITOR and MWAIT                                                */
#define IF_MONITORX         152 /* MONITORX and MWAITX                                              */
#define IF_WAITPKG          153 /* User wait instruction package                                    */
#define IF_MSR_IMM          154 /* Immediate RDMSR/WRMSRNS instructions                             */
#define IF_AESKLE           155 /* AES key locker                                                   */
#define IF_AESKLEWIDE_KL    156 /* AES wide key locker                                              */
#define IF_INVPCID          157 /* INVPCID instruction                                              */
#define IF_PREFETCHWT1      158 /* PREFETCHWT1 instruction                                          */
#define IF_PBNDKB           159 /* PBNDKB instruction                                               */
#define IF_PCONFIG          160 /* PCONFIG instruction                                              */
#define IF_WBNOINVD         161 /* WBNOINVD instruction                                             */
#define IF_SERIALIZE        162 /* SERIALIZE instruction                                            */
#define IF_LKGS             163 /* LKGS instruction                                                 */
#define IF_WRMSRNS          164 /* WRMSRNS instruction                                              */
#define IF_CLFLUSHOPT       165 /* CLFLUSHOPT instruction                                           */
#define IF_CLWB             166 /* CLWB instruction                                                 */
#define IF_RDRAND           167 /* RDRAND instruction                                               */
#define IF_RDSEED           168 /* RDSEED instruction                                               */
#define IF_RDPID            169 /* RDPID instruction                                                */
#define IF_LZCNT            170 /* LZCNT instruction                                                */
#define IF_PTWRITE          171 /* PTWRITE instruction                                              */
#define IF_CLDEMOTE         172 /* CLDEMOTE instruction                                             */
#define IF_MOVDIRI          173 /* MOVDIRI instruction                                              */
#define IF_MOVDIR64B        174 /* MOVDIR64B instruction                                            */
#define IF_CLZERO           175 /* CLZERO instruction                                               */
#define IF_MOVBE            176 /* MOVBE instruction                                                */
#define IF_MOVRS            177 /* MOVRS instruction                                                */
#define IF_OBSOLETE         178 /* Instruction removed from architecture                            */
#define IF_NEVER            179 /* Instruction never implemented                                    */
#define IF_NOP              180 /* Instruction is always a (nonintentional) NOP                     */
                                /* 180...191 reserved                                               */
#define IF_8086             192 /* 8086                                                             */
#define IF_186              193 /* 186+                                                             */
#define IF_286              194 /* 286+                                                             */
#define IF_386              195 /* 386+                                                             */
#define IF_486              196 /* 486+                                                             */
#define IF_PENT             197 /* Pentium                                                          */
#define IF_P6               198 /* P6                                                               */
#define IF_KATMAI           199 /* Katmai                                                           */
#define IF_WILLAMETTE       200 /* Willamette                                                       */
#define IF_PRESCOTT         201 /* Prescott                                                         */
#define IF_IA64             202 /* IA64 (in x86 mode)                                               */
#define IF_X86_64           203 /* x86-64 (long or legacy mode)                                     */
#define IF_NEHALEM          204 /* Nehalem                                                          */
#define IF_WESTMERE         205 /* Westmere                                                         */
#define IF_SANDYBRIDGE      206 /* Sandy Bridge                                                     */
#define IF_FUTURE           207 /* Ivy Bridge or newer                                              */
#define IF_DEFAULT          208 /* Default CPU level                                                */
#define IF_ANY              209 /* Allow any known instruction                                      */
#define IF_CYRIX            210 /* Cyrix-specific                                                   */
#define IF_AMD              211 /* AMD-specific                                                     */
                                /* 211...223 reserved                                               */

/* Mask bits for field 0 : 0...31 */
#define IFM_SM0             UINT32_C(0x00000001)     /*   0 */
#define IFM_SM1             UINT32_C(0x00000002)     /*   1 */
#define IFM_SM2             UINT32_C(0x00000004)     /*   2 */
#define IFM_SM3             UINT32_C(0x00000008)     /*   3 */
#define IFM_SM4             UINT32_C(0x00000010)     /*   4 */
#define IFM_AR0             UINT32_C(0x00000020)     /*   5 */
#define IFM_AR1             UINT32_C(0x00000040)     /*   6 */
#define IFM_AR2             UINT32_C(0x00000080)     /*   7 */
#define IFM_AR3             UINT32_C(0x00000100)     /*   8 */
#define IFM_AR4             UINT32_C(0x00000200)     /*   9 */
#define IFM_SB              UINT32_C(0x00000400)     /*  10 */
#define IFM_SW              UINT32_C(0x00000800)     /*  11 */
#define IFM_SD              UINT32_C(0x00001000)     /*  12 */
#define IFM_SQ              UINT32_C(0x00002000)     /*  13 */
#define IFM_ST              UINT32_C(0x00004000)     /*  14 */
#define IFM_SO              UINT32_C(0x00008000)     /*  15 */
#define IFM_SY              UINT32_C(0x00010000)     /*  16 */
#define IFM_SZ              UINT32_C(0x00020000)     /*  17 */
#define IFM_NWSIZE          UINT32_C(0x00040000)     /*  18 */
#define IFM_OSIZE           UINT32_C(0x00080000)     /*  19 */
#define IFM_ASIZE           UINT32_C(0x00100000)     /*  20 */
#define IFM_ANYSIZE         UINT32_C(0x00200000)     /*  21 */
#define IFM_SX              UINT32_C(0x00400000)     /*  22 */
#define IFM_SDWORD          UINT32_C(0x00800000)     /*  23 */
#define IFM_PSEUDO          UINT32_C(0x01000000)     /*  24 */
#define IFM_JMP_RELAX       UINT32_C(0x02000000)     /*  25 */
#define IFM_JCC_HINT        UINT32_C(0x04000000)     /*  26 */
#define IFM_OPT             UINT32_C(0x08000000)     /*  27 */
#define IFM_LATEVEX         UINT32_C(0x10000000)     /*  28 */
#define IFM_NOREX           UINT32_C(0x20000000)     /*  29 */
#define IFM_NOAPX           UINT32_C(0x40000000)     /*  30 */
#define IFM_NF              UINT32_C(0x80000000)     /*  31 */
/* Mask bits for field 1 : 32...63 */
#define IFM_NF_R            UINT32_C(0x00000001)     /*  32 */
#define IFM_NF_N            UINT32_C(0x00000002)     /*  33 */
#define IFM_NF_E            UINT32_C(0x00000004)     /*  34 */
#define IFM_ZU              UINT32_C(0x00000008)     /*  35 */
#define IFM_ZU_R            UINT32_C(0x00000010)     /*  36 */
#define IFM_ZU_E            UINT32_C(0x00000020)     /*  37 */
#define IFM_LIG             UINT32_C(0x00000040)     /*  38 */
#define IFM_WIG             UINT32_C(0x00000080)     /*  39 */
#define IFM_WW              UINT32_C(0x00000100)     /*  40 */
#define IFM_SIB             UINT32_C(0x00000200)     /*  41 */
#define IFM_LOCK            UINT32_C(0x00000400)     /*  42 */
#define IFM_LOCK1           UINT32_C(0x00000800)     /*  43 */
#define IFM_NOLONG          UINT32_C(0x00001000)     /*  44 */
#define IFM_LONG            UINT32_C(0x00002000)     /*  45 */
#define IFM_NOHLE           UINT32_C(0x00004000)     /*  46 */
#define IFM_MIB             UINT32_C(0x00008000)     /*  47 */
#define IFM_BND             UINT32_C(0x00010000)     /*  48 */
#define IFM_REX2            UINT32_C(0x00020000)     /*  49 */
#define IFM_HLE             UINT32_C(0x00040000)     /*  50 */
#define IFM_FL              UINT32_C(0x00080000)     /*  51 */
#define IFM_MOPVEC          UINT32_C(0x00100000)     /*  52 */
#define IFM_SCC             UINT32_C(0x00200000)     /*  53 */
#define IFM_BESTDIS         UINT32_C(0x00400000)     /*  54 */
#define IFM_DFV             UINT32_C(0x00800000)     /*  55 */
/* Mask bits for field 2 : 64...95 */
#define IFM_VEX             UINT32_C(0x00000001)     /*  64 */
#define IFM_EVEX            UINT32_C(0x00000002)     /*  65 */
#define IFM_PRIV            UINT32_C(0x00000004)     /*  66 */
#define IFM_SMM             UINT32_C(0x00000008)     /*  67 */
#define IFM_PROT            UINT32_C(0x00000010)     /*  68 */
#define IFM_UNDOC           UINT32_C(0x00000020)     /*  69 */
#define IFM_FPU             UINT32_C(0x00000040)     /*  70 */
#define IFM_MMX             UINT32_C(0x00000080)     /*  71 */
#define IFM_3DNOW           UINT32_C(0x00000100)     /*  72 */
#define IFM_SSE             UINT32_C(0x00000200)     /*  73 */
#define IFM_SSE2            UINT32_C(0x00000400)     /*  74 */
#define IFM_SSE3            UINT32_C(0x00000800)     /*  75 */
#define IFM_VMX             UINT32_C(0x00001000)     /*  76 */
#define IFM_SSSE3           UINT32_C(0x00002000)     /*  77 */
#define IFM_SSE4A           UINT32_C(0x00004000)     /*  78 */
#define IFM_SSE41           UINT32_C(0x00008000)     /*  79 */
#define IFM_SSE42           UINT32_C(0x00010000)     /*  80 */
#define IFM_SSE5            UINT32_C(0x00020000)     /*  81 */
#define IFM_AVX             UINT32_C(0x00040000)     /*  82 */
#define IFM_AVX2            UINT32_C(0x00080000)     /*  83 */
#define IFM_FMA             UINT32_C(0x00100000)     /*  84 */
#define IFM_BMI1            UINT32_C(0x00200000)     /*  85 */
#define IFM_BMI2            UINT32_C(0x00400000)     /*  86 */
#define IFM_TBM             UINT32_C(0x00800000)     /*  87 */
#define IFM_RTM             UINT32_C(0x01000000)     /*  88 */
#define IFM_AVX512          UINT32_C(0x02000000)     /*  89 */
#define IFM_AVX512F         UINT32_C(0x04000000)     /*  90 */
#define IFM_AVX512CD        UINT32_C(0x08000000)     /*  91 */
#define IFM_AVX512ER        UINT32_C(0x10000000)     /*  92 */
#define IFM_AVX512PF        UINT32_C(0x20000000)     /*  93 */
#define IFM_MPX             UINT32_C(0x40000000)     /*  94 */
#define IFM_SHA             UINT32_C(0x80000000)     /*  95 */
/* Mask bits for field 3 : 96...127 */
#define IFM_AVX512VL        UINT32_C(0x00000001)     /*  96 */
#define IFM_AVX512DQ        UINT32_C(0x00000002)     /*  97 */
#define IFM_AVX512BW        UINT32_C(0x00000004)     /*  98 */
#define IFM_AVX512IFMA      UINT32_C(0x00000008)     /*  99 */
#define IFM_AVX512VBMI      UINT32_C(0x00000010)     /* 100 */
#define IFM_AES             UINT32_C(0x00000020)     /* 101 */
#define IFM_VAES            UINT32_C(0x00000040)     /* 102 */
#define IFM_VPCLMULQDQ      UINT32_C(0x00000080)     /* 103 */
#define IFM_GFNI            UINT32_C(0x00000100)     /* 104 */
#define IFM_AVX512VBMI2     UINT32_C(0x00000200)     /* 105 */
#define IFM_AVX512VNNI      UINT32_C(0x00000400)     /* 106 */
#define IFM_AVX512BITALG    UINT32_C(0x00000800)     /* 107 */
#define IFM_AVX512VPOPCNTDQ UINT32_C(0x00001000)     /* 108 */
#define IFM_AVX5124FMAPS    UINT32_C(0x00002000)     /* 109 */
#define IFM_AVX5124VNNIW    UINT32_C(0x00004000)     /* 110 */
#define IFM_AVX512FP16      UINT32_C(0x00008000)     /* 111 */
#define IFM_F16C            UINT32_C(0x00010000)     /* 112 */
#define IFM_SGX             UINT32_C(0x00020000)     /* 113 */
#define IFM_CET             UINT32_C(0x00040000)     /* 114 */
#define IFM_ENQCMD          UINT32_C(0x00080000)     /* 115 */
#define IFM_TSXLDTRK        UINT32_C(0x00100000)     /* 116 */
#define IFM_AVX512BF16      UINT32_C(0x00200000)     /* 117 */
#define IFM_AVX512VP2INTERSECT UINT32_C(0x00400000)     /* 118 */
#define IFM_AMXTILE         UINT32_C(0x00800000)     /* 119 */
#define IFM_AMXBF16         UINT32_C(0x01000000)     /* 120 */
#define IFM_AMXFP16         UINT32_C(0x02000000)     /* 121 */
#define IFM_AMXFP8          UINT32_C(0x04000000)     /* 122 */
#define IFM_AMXTF32         UINT32_C(0x08000000)     /* 123 */
#define IFM_AMXINT8         UINT32_C(0x10000000)     /* 124 */
#define IFM_AMXCOMPLEX      UINT32_C(0x20000000)     /* 125 */
#define IFM_AMXAVX512       UINT32_C(0x40000000)     /* 126 */
#define IFM_AMXMOVRS        UINT32_C(0x80000000)     /* 127 */
/* Mask bits for field 4 : 128...159 */
#define IFM_AMXTRANSPOSE    UINT32_C(0x00000001)     /* 128 */
#define IFM_FRED            UINT32_C(0x00000002)     /* 129 */
#define IFM_RAOINT          UINT32_C(0x00000004)     /* 130 */
#define IFM_UINTR           UINT32_C(0x00000008)     /* 131 */
#define IFM_CMPCCXADD       UINT32_C(0x00000010)     /* 132 */
#define IFM_PREFETCHI       UINT32_C(0x00000020)     /* 133 */
#define IFM_MSRLIST         UINT32_C(0x00000040)     /* 134 */
#define IFM_AVXNECONVERT    UINT32_C(0x00000080)     /* 135 */
#define IFM_AVXVNNI         UINT32_C(0x00000100)     /* 136 */
#define IFM_AVXVNNIINT8     UINT32_C(0x00000200)     /* 137 */
#define IFM_AVXVNNIINT16    UINT32_C(0x00000400)     /* 138 */
#define IFM_AVXIFMA         UINT32_C(0x00000800)     /* 139 */
#define IFM_HRESET          UINT32_C(0x00001000)     /* 140 */
#define IFM_SMAP            UINT32_C(0x00002000)     /* 141 */
#define IFM_SHA512          UINT32_C(0x00004000)     /* 142 */
#define IFM_HSM3            UINT32_C(0x00008000)     /* 143 */
#define IFM_HSM4            UINT32_C(0x00010000)     /* 144 */
#define IFM_APX             UINT32_C(0x00020000)     /* 145 */
#define IFM_AVX10_1         UINT32_C(0x00040000)     /* 146 */
#define IFM_AVX10_2         UINT32_C(0x00080000)     /* 147 */
#define IFM_AVX10_VNNIINT   UINT32_C(0x00100000)     /* 148 */
#define IFM_ADX             UINT32_C(0x00200000)     /* 149 */
#define IFM_PKU             UINT32_C(0x00400000)     /* 150 */
#define IFM_MONITOR         UINT32_C(0x00800000)     /* 151 */
#define IFM_MONITORX        UINT32_C(0x01000000)     /* 152 */
#define IFM_WAITPKG         UINT32_C(0x02000000)     /* 153 */
#define IFM_MSR_IMM         UINT32_C(0x04000000)     /* 154 */
#define IFM_AESKLE          UINT32_C(0x08000000)     /* 155 */
#define IFM_AESKLEWIDE_KL   UINT32_C(0x10000000)     /* 156 */
#define IFM_INVPCID         UINT32_C(0x20000000)     /* 157 */
#define IFM_PREFETCHWT1     UINT32_C(0x40000000)     /* 158 */
#define IFM_PBNDKB          UINT32_C(0x80000000)     /* 159 */
/* Mask bits for field 5 : 160...191 */
#define IFM_PCONFIG         UINT32_C(0x00000001)     /* 160 */
#define IFM_WBNOINVD        UINT32_C(0x00000002)     /* 161 */
#define IFM_SERIALIZE       UINT32_C(0x00000004)     /* 162 */
#define IFM_LKGS            UINT32_C(0x00000008)     /* 163 */
#define IFM_WRMSRNS         UINT32_C(0x00000010)     /* 164 */
#define IFM_CLFLUSHOPT      UINT32_C(0x00000020)     /* 165 */
#define IFM_CLWB            UINT32_C(0x00000040)     /* 166 */
#define IFM_RDRAND          UINT32_C(0x00000080)     /* 167 */
#define IFM_RDSEED          UINT32_C(0x00000100)     /* 168 */
#define IFM_RDPID           UINT32_C(0x00000200)     /* 169 */
#define IFM_LZCNT           UINT32_C(0x00000400)     /* 170 */
#define IFM_PTWRITE         UINT32_C(0x00000800)     /* 171 */
#define IFM_CLDEMOTE        UINT32_C(0x00001000)     /* 172 */
#define IFM_MOVDIRI         UINT32_C(0x00002000)     /* 173 */
#define IFM_MOVDIR64B       UINT32_C(0x00004000)     /* 174 */
#define IFM_CLZERO          UINT32_C(0x00008000)     /* 175 */
#define IFM_MOVBE           UINT32_C(0x00010000)     /* 176 */
#define IFM_MOVRS           UINT32_C(0x00020000)     /* 177 */
#define IFM_OBSOLETE        UINT32_C(0x00040000)     /* 178 */
#define IFM_NEVER           UINT32_C(0x00080000)     /* 179 */
#define IFM_NOP             UINT32_C(0x00100000)     /* 180 */
/* Mask bits for field 6 : 192...223 */
#define IFM_8086            UINT32_C(0x00000001)     /* 192 */
#define IFM_186             UINT32_C(0x00000002)     /* 193 */
#define IFM_286             UINT32_C(0x00000004)     /* 194 */
#define IFM_386             UINT32_C(0x00000008)     /* 195 */
#define IFM_486             UINT32_C(0x00000010)     /* 196 */
#define IFM_PENT            UINT32_C(0x00000020)     /* 197 */
#define IFM_P6              UINT32_C(0x00000040)     /* 198 */
#define IFM_KATMAI          UINT32_C(0x00000080)     /* 199 */
#define IFM_WILLAMETTE      UINT32_C(0x00000100)     /* 200 */
#define IFM_PRESCOTT        UINT32_C(0x00000200)     /* 201 */
#define IFM_IA64            UINT32_C(0x00000400)     /* 202 */
#define IFM_X86_64          UINT32_C(0x00000800)     /* 203 */
#define IFM_NEHALEM         UINT32_C(0x00001000)     /* 204 */
#define IFM_WESTMERE        UINT32_C(0x00002000)     /* 205 */
#define IFM_SANDYBRIDGE     UINT32_C(0x00004000)     /* 206 */
#define IFM_FUTURE          UINT32_C(0x00008000)     /* 207 */
#define IFM_DEFAULT         UINT32_C(0x00010000)     /* 208 */
#define IFM_ANY             UINT32_C(0x00020000)     /* 209 */
#define IFM_CYRIX           UINT32_C(0x00040000)     /* 210 */
#define IFM_AMD             UINT32_C(0x00080000)     /* 211 */

/* IF_SM0 (0) ... IF_DFV (55) */
#define IF_IGEN_FIRST         0
#define IF_IGEN_COUNT        56
#define IF_IGEN_FIELD         0
#define IF_IGEN_NFIELDS       2

/* IF_VEX (64) ... IF_NOP (180) */
#define IF_FEATURE_FIRST     64
#define IF_FEATURE_COUNT    117
#define IF_FEATURE_FIELD      2
#define IF_FEATURE_NFIELDS    4

/* IF_8086 (192) ... IF_AMD (211) */
#define IF_CPU_FIRST        192
#define IF_CPU_COUNT         20
#define IF_CPU_FIELD          6
#define IF_CPU_NFIELDS        1

#define IF_FIELD_COUNT 7
typedef struct {
    uint32_t field[IF_FIELD_COUNT];
} iflag_t;

/* All combinations of instruction flags used in instruction patterns */
extern const iflag_t insns_flags[655];

#endif /* NASM_IFLAGGEN_H */
