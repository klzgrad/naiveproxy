# -*- perl -*-

#
# dword bound, index 0 - specific flags
#
if_align('IGEN');

if_("SM",                "Size match");
if_("SM2",               "Size match first two operands");
if_("SB",                "Unsized operands can't be non-byte");
if_("SW",                "Unsized operands can't be non-word");
if_("SD",                "Unsized operands can't be non-dword");
if_("SQ",                "Unsized operands can't be non-qword");
if_("SO",                "Unsized operands can't be non-oword");
if_("SY",                "Unsized operands can't be non-yword");
if_("SZ",                "Unsized operands can't be non-zword");
if_("SIZE",              "Unsized operands must match the bitsize");
if_("SX",                "Unsized operands not allowed");
if_("ANYSIZE",           "Ignore operand size even if explicit");
if_("AR0",               "SB, SW, SD applies to argument 0");
if_("AR1",               "SB, SW, SD applies to argument 1");
if_("AR2",               "SB, SW, SD applies to argument 2");
if_("AR3",               "SB, SW, SD applies to argument 3");
if_("AR4",               "SB, SW, SD applies to argument 4");
if_("OPT",               "Optimizing assembly only");

#
# dword bound - instruction feature filtering flags
#
if_align('FEATURE');

if_("PRIV",              "Privileged instruction");
if_("SMM",               "Only valid in SMM");
if_("PROT",              "Protected mode only");
if_("LOCK",              "Lockable if operand 0 is memory");
if_("NOLONG",            "Not available in long mode");
if_("LONG",              "Long mode");
if_("NOHLE",             "HLE prefixes forbidden");
if_("MIB",               "disassemble with split EA");
if_("BND",               "BND (0xF2) prefix available");
if_("UNDOC",             "Undocumented");
if_("HLE",               "HLE prefixed");
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
if_("FMA",               "");
if_("BMI1",              "");
if_("BMI2",              "");
if_("TBM",               "");
if_("RTM",               "");
if_("INVPCID",           "");
if_("AVX512",            "AVX-512F (512-bit base architecture)");
if_("AVX512CD",          "AVX-512 Conflict Detection");
if_("AVX512ER",          "AVX-512 Exponential and Reciprocal");
if_("AVX512PF",          "AVX-512 Prefetch");
if_("MPX",               "MPX");
if_("SHA",               "SHA");
if_("PREFETCHWT1",       "PREFETCHWT1");
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
if_("SGX",               "Intel Software Guard Extensions (SGX)");
if_("CET",               "Intel Control-Flow Enforcement Technology (CET)");

# Put these last [hpa: why?]
if_("OBSOLETE",          "Instruction removed from architecture");
if_("NEVER",             "Instruction never implemented");
if_("NOP",               "Instruction is always a (nonintentional) NOP");
if_("VEX",               "VEX or XOP encoded instruction");
if_("EVEX",              "EVEX encoded instruction");

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
if_("X86_64",            "x86-64 (long or legacy mode)");
if_("NEHALEM",           "Nehalem");
if_("WESTMERE",          "Westmere");
if_("SANDYBRIDGE",       "Sandy Bridge");
if_("FUTURE",            "Ivy Bridge or newer");
if_("IA64",              "IA64 (in x86 mode)");

# Must be the last CPU definition
if_("ANY",               "Any x86 CPU");

# These must come after the CPU defitions proper
if_("CYRIX",             "Cyrix-specific");
if_("AMD",               "AMD-specific");
