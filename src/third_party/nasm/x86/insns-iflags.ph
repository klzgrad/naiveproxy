#!/usr/bin/perl
## --------------------------------------------------------------------------
##
##   Copyright 1996-2018 The NASM Authors - All Rights Reserved
##   See the file AUTHORS included with the NASM distribution for
##   the specific copyright holders.
##
##   Redistribution and use in source and binary forms, with or without
##   modification, are permitted provided that the following
##   conditions are met:
##
##   * Redistributions of source code must retain the above copyright
##     notice, this list of conditions and the following disclaimer.
##   * Redistributions in binary form must reproduce the above
##     copyright notice, this list of conditions and the following
##     disclaimer in the documentation and/or other materials provided
##     with the distribution.
##
##     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
##     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
##     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
##     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
##     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
##     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
##     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
##     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
##     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
##     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
##     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
##     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
##     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
##
## --------------------------------------------------------------------------

#
# Instruction template flags. These specify which processor
# targets the instruction is eligible for, whether it is
# privileged or undocumented, and also specify extra error
# checking on the matching of the instruction.
#
# IF_SM stands for Size Match: any operand whose size is not
# explicitly specified by the template is `really' intended to be
# the same size as the first size-specified operand.
# Non-specification is tolerated in the input instruction, but
# _wrong_ specification is not.
#
# IF_SM2 invokes Size Match on only the first _two_ operands, for
# three-operand instructions such as SHLD: it implies that the
# first two operands must match in size, but that the third is
# required to be _unspecified_.
#
# IF_SB invokes Size Byte: operands with unspecified size in the
# template are really bytes, and so no non-byte specification in
# the input instruction will be tolerated. IF_SW similarly invokes
# Size Word, and IF_SD invokes Size Doubleword.
#
# (The default state if neither IF_SM nor IF_SM2 is specified is
# that any operand with unspecified size in the template is
# required to have unspecified size in the instruction too...)
#
# iflag_t is defined to store these flags.
#
# The order does matter here. We use some predefined masks to quick test
# for a set of flags, so be careful moving bits (and
# don't forget to update C code generation then).
#
sub dword_align($) {
    my($n) = @_;

    $$n = ($$n + 31) & ~31;
    return $n;
}

my $f = 0;
my %insns_flag_bit = (
    #
    # dword bound, index 0 - specific flags
    #
    "SM"                => [$f++, "Size match"],
    "SM2"               => [$f++, "Size match first two operands"],
    "SB"                => [$f++, "Unsized operands can't be non-byte"],
    "SW"                => [$f++, "Unsized operands can't be non-word"],
    "SD"                => [$f++, "Unsized operands can't be non-dword"],
    "SQ"                => [$f++, "Unsized operands can't be non-qword"],
    "SO"                => [$f++, "Unsized operands can't be non-oword"],
    "SY"                => [$f++, "Unsized operands can't be non-yword"],
    "SZ"                => [$f++, "Unsized operands can't be non-zword"],
    "SIZE"              => [$f++, "Unsized operands must match the bitsize"],
    "SX"                => [$f++, "Unsized operands not allowed"],
    "AR0"               => [$f++, "SB, SW, SD applies to argument 0"],
    "AR1"               => [$f++, "SB, SW, SD applies to argument 1"],
    "AR2"               => [$f++, "SB, SW, SD applies to argument 2"],
    "AR3"               => [$f++, "SB, SW, SD applies to argument 3"],
    "AR4"               => [$f++, "SB, SW, SD applies to argument 4"],
    "OPT"               => [$f++, "Optimizing assembly only"],

    #
    # dword bound - instruction filtering flags
    #
    "PRIV"              => [${dword_align(\$f)}++, "Privileged instruction"],
    "SMM"               => [$f++, "Only valid in SMM"],
    "PROT"              => [$f++, "Protected mode only"],
    "LOCK"              => [$f++, "Lockable if operand 0 is memory"],
    "NOLONG"            => [$f++, "Not available in long mode"],
    "LONG"              => [$f++, "Long mode"],
    "NOHLE"             => [$f++, "HLE prefixes forbidden"],
    "MIB"               => [$f++, "disassemble with split EA"],
    "BND"               => [$f++, "BND (0xF2) prefix available"],
    "UNDOC"             => [$f++, "Undocumented"],
    "HLE"               => [$f++, "HLE prefixed"],
    "FPU"               => [$f++, "FPU"],
    "MMX"               => [$f++, "MMX"],
    "3DNOW"             => [$f++, "3DNow!"],
    "SSE"               => [$f++, "SSE (KNI, MMX2)"],
    "SSE2"              => [$f++, "SSE2"],
    "SSE3"              => [$f++, "SSE3 (PNI)"],
    "VMX"               => [$f++, "VMX"],
    "SSSE3"             => [$f++, "SSSE3"],
    "SSE4A"             => [$f++, "AMD SSE4a"],
    "SSE41"             => [$f++, "SSE4.1"],
    "SSE42"             => [$f++, "SSE4.2"],
    "SSE5"              => [$f++, "SSE5"],
    "AVX"               => [$f++, "AVX  (256-bit floating point)"],
    "AVX2"              => [$f++, "AVX2 (256-bit integer)"],
    "FMA"               => [$f++, ""],
    "BMI1"              => [$f++, ""],
    "BMI2"              => [$f++, ""],
    "TBM"               => [$f++, ""],
    "RTM"               => [$f++, ""],
    "INVPCID"           => [$f++, ""],
    "AVX512"            => [$f++, "AVX-512F (512-bit base architecture)"],
    "AVX512CD"          => [$f++, "AVX-512 Conflict Detection"],
    "AVX512ER"          => [$f++, "AVX-512 Exponential and Reciprocal"],
    "AVX512PF"          => [$f++, "AVX-512 Prefetch"],
    "MPX"               => [$f++, "MPX"],
    "SHA"               => [$f++, "SHA"],
    "PREFETCHWT1"       => [$f++, "PREFETCHWT1"],
    "AVX512VL"          => [$f++, "AVX-512 Vector Length Orthogonality"],
    "AVX512DQ"          => [$f++, "AVX-512 Dword and Qword"],
    "AVX512BW"          => [$f++, "AVX-512 Byte and Word"],
    "AVX512IFMA"        => [$f++, "AVX-512 IFMA instructions"],
    "AVX512VBMI"        => [$f++, "AVX-512 VBMI instructions"],
    "AES"               => [$f++, "AES instructions"],
    "VAES"              => [$f++, "AES AVX instructions"],
    "VPCLMULQDQ"        => [$f++, "AVX Carryless Multiplication"],
    "GFNI"		=> [$f++, "Galois Field instructions"],
    "AVX512VBMI2"       => [$f++, "AVX-512 VBMI2 instructions"],
    "AVX512VNNI"        => [$f++, "AVX-512 VNNI instructions"],
    "AVX512BITALG"	=> [$f++, "AVX-512 Bit Algorithm instructions"],
    "AVX512VPOPCNTDQ"	=> [$f++, "AVX-512 VPOPCNTD/VPOPCNTQ"],
    "AVX5124FMAPS"	=> [$f++, "AVX-512 4-iteration multiply-add"],
    "AVX5124VNNIW"	=> [$f++, "AVX-512 4-iteration dot product"],
    "SGX"		=> [$f++, "Intel Software Guard Extensions (SGX)"],

    # Put these last
    "OBSOLETE"          => [$f++, "Instruction removed from architecture"],
    "VEX"               => [$f++, "VEX or XOP encoded instruction"],
    "EVEX"              => [$f++, "EVEX encoded instruction"],

    #
    # dword bound - cpu type flags
    #
    # The CYRIX and AMD flags should have the highest bit values; the
    # disassembler selection algorithm depends on it.
    #
    "8086"              => [${dword_align(\$f)}++, "8086"],
    "186"               => [$f++, "186+"],
    "286"               => [$f++, "286+"],
    "386"               => [$f++, "386+"],
    "486"               => [$f++, "486+"],
    "PENT"              => [$f++, "Pentium"],
    "P6"                => [$f++, "P6"],
    "KATMAI"            => [$f++, "Katmai"],
    "WILLAMETTE"        => [$f++, "Willamette"],
    "PRESCOTT"          => [$f++, "Prescott"],
    "X86_64"            => [$f++, "x86-64 (long or legacy mode)"],
    "NEHALEM"           => [$f++, "Nehalem"],
    "WESTMERE"          => [$f++, "Westmere"],
    "SANDYBRIDGE"       => [$f++, "Sandy Bridge"],
    "FUTURE"            => [$f++, "Future processor (not yet disclosed)"],
    "IA64"              => [$f++, "IA64 (in x86 mode)"],

    # Put these last
    "CYRIX"             => [$f++, "Cyrix-specific"],
    "AMD"               => [$f++, "AMD-specific"],
);

my %insns_flag_hash = ();
my @insns_flag_values = ();
my $iflag_words;

sub get_flag_words() {
    my $max = -1;

    foreach my $vp (values(%insns_flag_bit)) {
	if ($vp->[0] > $max) {
	    $max = $vp->[0];
	}
    }

    return int($max/32)+1;
}

sub insns_flag_index(@) {
    return undef if $_[0] eq "ignore";

    my @prekey = sort(@_);
    my $key = join("", @prekey);

    if (not defined($insns_flag_hash{$key})) {
        my @newkey = (0) x $iflag_words;

        for my $i (@prekey) {
            die "No key for $i\n" if not defined($insns_flag_bit{$i});
	    $newkey[$insns_flag_bit{$i}[0]/32] |=
		(1 << ($insns_flag_bit{$i}[0] % 32));
        }

	my $str = join(',', map { sprintf("UINT32_C(0x%08x)",$_) } @newkey);

        push @insns_flag_values, $str;
        $insns_flag_hash{$key} = $#insns_flag_values;
    }

    return $insns_flag_hash{$key};
}

sub write_iflaggen_h() {
    print STDERR "Writing $oname...\n";

    open(N, '>', $oname) or die "$0: $!\n";

    print N "/* This file is auto-generated. Don't edit. */\n";
    print N "#ifndef NASM_IFLAGGEN_H\n";
    print N "#define NASM_IFLAGGEN_H 1\n\n";

    my @flagnames = keys(%insns_flag_bit);
    @flagnames = sort {
	$insns_flag_bit{$a}->[0] <=> $insns_flag_bit{$b}->[0]
    } @flagnames;
    my $next = 0;
    foreach my $key (@flagnames) {
	my $v = $insns_flag_bit{$key};
	if ($v->[0] > $next) {
	    printf N "%-31s /* %-64s */\n", '',
		($next != $v->[0]-1) ?
		sprintf("%d...%d unused", $next, $v->[0]-1) :
		sprintf("%d unused", $next);
	}
        print N sprintf("#define IF_%-16s %3d /* %-64s */\n",
			$key, $v->[0], $v->[1]);
	$next = $v->[0] + 1;
    }

    print N "\n";
    printf N "#define IF_FIELD_COUNT %d\n", $iflag_words;
    print N "typedef struct {\n";
    print N "    uint32_t field[IF_FIELD_COUNT];\n";
    print N "} iflag_t;\n";

    print N "\n";
    printf N "extern const iflag_t insns_flags[%d];\n\n",
	$#insns_flag_values + 1;

    print N "#endif /* NASM_IFLAGGEN_H */\n";
    close N;
}

sub write_iflag_c() {
    print STDERR "Writing $oname...\n";

    open(N, '>', $oname) or die "$0: $!\n";

    print N "/* This file is auto-generated. Don't edit. */\n";
    print N "#include \"iflag.h\"\n\n";
    print N "/* Global flags referenced from instruction templates */\n";
    printf N "const iflag_t insns_flags[%d] = {\n",
        $#insns_flag_values + 1;
    foreach my $i (0 .. $#insns_flag_values) {
        print N sprintf("    /* %4d */ {{ %s }},\n", $i, $insns_flag_values[$i]);
    }
    print N "};\n\n";
    close N;
}

$iflag_words = get_flag_words();

1;
