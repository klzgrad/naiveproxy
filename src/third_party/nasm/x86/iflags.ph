# -*- perl -*-

#
# Flags consumed by preinsns.pl/insns.pl and not actually present in
# the generated C code:
#
#  ND		- not for disassembler
#  DISASM	- disassembler only
#  KILL		- ignore this instruction pattern entirely
#  !ZU		- no zero-upper version of this instruction
#  !FL          - instruction never modifies the flags
#  NF!          - alias for NF_R: {nf} syntactic prefix required
#  SMx-y        - alias for SMx,SMx+1,SMx+2,...,SMy
#

#
# dword bound, index 0 - specific flags
#
if_align('IGEN', $NOBREAK);

# The following MUST be in word 0
if_("SM0",               "Size match operand 0");
if_("SM1",               "Size match operand 1");
if_("SM2",               "Size match operand 2");
if_("SM3",               "Size match operand 3");
if_("SM4",               "Size match operand 4");
if_("AR0",               "SB, SW, SD applies to operand 0");
if_("AR1",               "SB, SW, SD applies to operand 1");
if_("AR2",               "SB, SW, SD applies to operand 2");
if_("AR3",               "SB, SW, SD applies to operand 3");
if_("AR4",               "SB, SW, SD applies to operand 4");
# These must match the order of the BITSx flags in opflags.h
if_("SB",                "Unsized operands can't be non-byte");
if_("SW",                "Unsized operands can't be non-word");
if_("SD",                "Unsized operands can't be non-dword");
if_("SQ",                "Unsized operands can't be non-qword");
if_("ST",                "Unsized operands can't be non-tword");
if_("SO",                "Unsized operands can't be non-oword");
if_("SY",                "Unsized operands can't be non-yword");
if_("SZ",                "Unsized operands can't be non-zword");
# End BITSx order match requirement
if_("NWSIZE",            "Operand size defaults to 64 in 64-bit mode");
# OSIZE can be modified by osp prefixes, but not by other operands
if_("OSIZE",             "Unsized operands must match the operand size");
if_("ASIZE",             "Unsized operands must match the address size");
if_("ANYSIZE",           "Ignore operand size even if explicit");
if_("SX",                "Unsized operands not allowed");
if_("SDWORD",		 "Strict sdword64 matching");
if_break_ok();

if_("PSEUDO",            "Pseudo-instruction (directive)");
if_("JMP_RELAX",         "Relaxable jump instruction");
if_("JCC_HINT",          "Hintable jump instruction");
if_("OPT",               "Optimizing assembly only");
if_("LATEVEX",           "Only if EVEX instructions are disabled");
if_("NOREX",             "Instruction does not support REX encoding");
if_("NOAPX",             "Instruction does not support APX registers or REX2");
if_("NF",                "Instruction supports the {nf} prefix");
if_("NF_R",              "Instruction requires the {nf} prefix");
if_("NF_N",              "Instruction doesn't allow the {nf} prefix");
if_("NF_E",              "EVEX.NF set with {nf} prefix");
if_("ZU",                "Instruction supports the {zu} prefix");
if_("ZU_R",              "Instruction requires the {zu} prefix");
if_("ZU_E",              "EVEX.ND set with {zu} prefix");
if_("LIG",               "Ignore VEX/EVEX L field");
if_("WIG",               "Ignore VEX/EVEX W field");
if_("WW",                "VEX/EVEX W is REX.W");
if_("SIB",               "SIB encoding required");
if_("LOCK",              "Lockable if operand 0 is memory");
if_("LOCK1",             "Lockable if operand 1 is memory");
if_("NOLONG",            "Not available in long mode");
if_("LONG",              "Long mode");
if_("NOHLE",             "HLE prefixes forbidden");
if_("MIB",               "split base/index EA");
if_("BND",               "BND (0xF2) prefix available");
if_("REX2",              "REX2 encoding required");
if_("HLE",               "HLE prefixed");
if_("FL",                "Instruction modifies the flags");
if_("MOPVEC",		 "M operand is a vector"); # Autodetected
if_("SCC",		 "EVEX[27:24] is special condition code");
if_("BESTDIS",           "Preferred disassembly pattern");

#
# Special immediates types like {dfv=}
# Used to detect incorrect usage and for the disassembler.
#
if_("DFV",               "Destination flag values");

#
# dword bound - instruction feature filtering flags
#
if_align('FEATURE');

#
# Encoding formats that can be set with the CPU directive
#
if_("VEX",               "VEX or XOP encoded instruction");
if_("EVEX",              "EVEX encoded instruction");

#
# Feature filtering flags
#
if_("PRIV",              "Privileged instruction");
if_("SMM",               "Only valid in SMM");
if_("PROT",              "Protected mode only");
if_("UNDOC",             "Undocumented");
if_("FPU",               "FPU");
if_("MMX",               "MMX");
if_("3DNOW",             "3DNow!");
if_("SSE",               "SSE (KNI, MMX2)");
if_("SSE2",              "SSE2");
if_("SSE3",              "SSE3 (PNI)");
if_("VMX",               "VMX");
if_("SSSE3",             "SSSE3");
if_("SSE4A",             "AMD SSE4a");
if_("SSE41",             "SSE4.1");
if_("SSE42",             "SSE4.2");
if_("SSE5",              "SSE5");
if_("AVX",               "AVX  (256-bit floating point)");
if_("AVX2",              "AVX2 (256-bit integer)");
if_("FMA",               "Fused multiply-add");
if_("BMI1",              "Bit manipulation instructions 1");
if_("BMI2",              "Bit manipulation instructions 2");
if_("TBM",               "");
if_("RTM",               "");
if_("AVX512",            "AVX-512");
if_("AVX512F",           "AVX-512F (base architecture)");
if_("AVX512CD",          "AVX-512 Conflict Detection");
if_("AVX512ER",          "AVX-512 Exponential and Reciprocal");
if_("AVX512PF",          "AVX-512 Prefetch");
if_("MPX",               "MPX");
if_("SHA",               "SHA");
if_("AVX512VL",          "AVX-512 Vector Length Orthogonality");
if_("AVX512DQ",          "AVX-512 Dword and Qword");
if_("AVX512BW",          "AVX-512 Byte and Word");
if_("AVX512IFMA",        "AVX-512 IFMA instructions");
if_("AVX512VBMI",        "AVX-512 VBMI instructions");
if_("AES",               "AES instructions");
if_("VAES",              "AES AVX instructions");
if_("VPCLMULQDQ",        "AVX Carryless Multiplication");
if_("GFNI",              "Galois Field instructions");
if_("AVX512VBMI2",       "AVX-512 VBMI2 instructions");
if_("AVX512VNNI",        "AVX-512 VNNI instructions");
if_("AVX512BITALG",      "AVX-512 Bit Algorithm instructions");
if_("AVX512VPOPCNTDQ",   "AVX-512 VPOPCNTD/VPOPCNTQ");
if_("AVX5124FMAPS",      "AVX-512 4-iteration multiply-add");
if_("AVX5124VNNIW",      "AVX-512 4-iteration dot product");
if_("AVX512FP16",        "AVX-512 FP16 instructions");
if_("F16C",              "F16C instructions");
if_("SGX",               "Intel Software Guard Extensions (SGX)");
if_("CET",               "Intel Control-Flow Enforcement Technology (CET)");
if_("ENQCMD",            "Enqueue command instructions");
if_("TSXLDTRK",          "TSX suspend load address tracking");
if_("AVX512BF16",        "AVX-512 bfloat16");
if_("AVX512VP2INTERSECT", "AVX-512 VP2INTERSECT instructions");
if_("AMXTILE",           "AMX tile configuration instructions");
if_("AMXBF16",           "AMX bfloat16 multiplication");
if_("AMXFP16",           "AMX FP16 multiplication");
if_("AMXFP8",            "AMX FP8 instructions");
if_("AMXTF32",           "AMX TF32 multiplication");
if_("AMXINT8",           "AMX 8-bit integer multiplication");
if_("AMXCOMPLEX",        "AMX float16 complex multiplication");
if_("AMXAVX512",         "EVEX zmm<-tmm conversion instructions");
if_("AMXMOVRS",          "AMX loads with MOVRS hint");
if_("AMXTRANSPOSE",      "AMX transpose instructions");
if_("FRED",              "Flexible Return and Exception Delivery (FRED)");
if_("RAOINT",		 "Remote atomic operations (RAO-INT)");
if_("UINTR",		 "User interrupts");
if_("CMPCCXADD",         "CMPccXADD instructions");
if_("PREFETCHI",         "PREFETCHI0 and PREFETCHI1");
if_("MSRLIST",           "RDMSRLIST and WRMSRLIST");
if_("AVXNECONVERT",	 "AVX exceptionless floating-point conversions");
if_("AVXVNNI",           "AVX Vector Neural Network instructions");
if_("AVXVNNIINT8",       "AVX Vector Neural Network 8-bit integer instructions");
if_("AVXVNNIINT16",      "AVX Vector Neural Network 16-bit integer instructions");
if_("AVXIFMA",           "AVX integer multiply and add");
if_("HRESET",            "History reset");
if_("SMAP",		 "Supervisor Mode Access Prevention (SMAP)");
if_("SHA512",            "SHA512 instructions");
if_("HSM3",              "SM3 hash instructions");
if_("HSM4",              "SM4 hash instructions");
if_("APX",               "Advanced Performance Extensions (APX)");
if_("AVX10_1",           "AVX 10.1 instructions");
if_("AVX10_2",           "AVX 10.2 instructions");
if_("AVX10_VNNIINT",     "AVX Vector Neural Network integer instructions");
if_("ADX",               "ADCX and ADOX instructions");
if_("PKU",		 "Protection key for user mode");
if_("MONITOR",		 "MONITOR and MWAIT");
if_("MONITORX",		 "MONITORX and MWAITX");
if_("WAITPKG",           "User wait instruction package");
if_("MSR_IMM",		 "Immediate RDMSR/WRMSRNS instructions");
if_("AESKLE",            "AES key locker");
if_("AESKLEWIDE_KL",     "AES wide key locker");

# Single-instruction CPUID bits without additional help text
my @oneins = qw(invpcid prefetchwt1 pbndkb pconfig wbnoinvd serialize
		lkgs wrmsrns clflushopt clwb rdrand rdseed rdpid lzcnt
		ptwrite cldemote movdiri movdir64b clzero movbe movrs);
foreach my $ins (@oneins) {
    if_($ins, "\U$ins\E instruction");
}

# Put these last to minimize their relevance
if_("OBSOLETE",          "Instruction removed from architecture");
if_("NEVER",             "Instruction never implemented");
if_("NOP",               "Instruction is always a (nonintentional) NOP");

#
# dword bound - cpu type flags
#
# The CYRIX and AMD flags should have the highest bit values; the
# disassembler selection algorithm depends on it.
#
if_align('CPU');

if_("8086",              "8086");
if_("186",               "186+");
if_("286",               "286+");
if_("386",               "386+");
if_("486",               "486+");
if_("PENT",              "Pentium");
if_("P6",                "P6");
if_("KATMAI",            "Katmai");
if_("WILLAMETTE",        "Willamette");
if_("PRESCOTT",          "Prescott");
if_("IA64",              "IA64 (in x86 mode)");
if_("X86_64",            "x86-64 (long or legacy mode)");
if_("NEHALEM",           "Nehalem");
if_("WESTMERE",          "Westmere");
if_("SANDYBRIDGE",       "Sandy Bridge");
if_("FUTURE",            "Ivy Bridge or newer");

# Default CPU level
if_("DEFAULT",           "Default CPU level");

# Must be the last CPU definition
if_("ANY",               "Allow any known instruction");

# These must come after the CPU definitions proper
if_("CYRIX",             "Cyrix-specific");
if_("AMD",               "AMD-specific");
