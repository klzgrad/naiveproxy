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


my $n_iflags = 0;
my %flag_byname;
my @flag_bynum;
my @flag_fields;
my $iflag_words;

sub if_($$) {
    my($name, $def) = @_;
    my $num = $n_iflags++;
    my $v = [$num, $name, $def];

    $flag_byname{$name}  = $v;
    $flag_bynum[$num] = $v;

    return 1;
}
sub if_align($) {
    my($name) = @_;

    if ($#flag_fields >= 0) {
	$flag_fields[$#flag_fields]->[2] = $n_iflags-1;
    }
    $n_iflags = ($n_iflags + 31) & ~31;

    if (defined($name)) {
	push(@flag_fields, [$name, $n_iflags, undef]);
    }

    return 1;
}

sub if_end() {
    if_align(undef);
    $iflag_words = $n_iflags >> 5;
}

# The actual flags defintions
require 'x86/iflags.ph';
if_end();

# Compute the combinations of instruction flags actually used in templates

my %insns_flag_hash = ();
my @insns_flag_values = ();
my @insns_flag_lists = ();

sub insns_flag_index(@) {
    return undef if $_[0] eq "ignore";

    my @prekey = sort(@_);
    my $key = join(',', @prekey);
    my $flag_index = $insns_flag_hash{$key};

    unless (defined($flag_index)) {
        my @newkey = (0) x $iflag_words;

        foreach my $i (@prekey) {
	    my $flag = $flag_byname{$i};
            die "No key for $i (in $key)\n" if not defined($flag);
	    $newkey[$flag->[0] >> 5] |= (1 << ($flag->[0] & 31));
        }

	my $str = join(',', map { sprintf("UINT32_C(0x%08x)",$_) } @newkey);

        push @insns_flag_values, $str;
	push @insns_flag_lists, $key;
        $insns_flag_hash{$key} = $flag_index = $#insns_flag_values;
    }

    return $flag_index;
}

sub write_iflaggen_h() {
    print STDERR "Writing $oname...\n";

    open(N, '>', $oname) or die "$0: $!\n";

    print N "/* This file is auto-generated. Don't edit. */\n";
    print N "#ifndef NASM_IFLAGGEN_H\n";
    print N "#define NASM_IFLAGGEN_H 1\n\n";

    # The flag numbers; the <= in the loop is intentional

    my $next = 0;
    for ($i = 0; $i <= $n_iflags; $i++) {
	if ((defined($flag_bynum[$i]) || $i >= $n_iflags) &&
	    $next != $i) {
	    printf N "%-31s /* %-64s */\n", '',
		($next < $i-1) ?
		sprintf("%d...%d reserved", $next-1, $i-1) :
		sprintf("%d reserved", $i-1);
	}

	if (defined($flag_bynum[$i])) {
	    printf N "#define IF_%-16s %3d /* %-64s */\n",
		$flag_bynum[$i]->[1], $i, $flag_bynum[$i]->[2];
	    $next = $i+1;
	}
    }
    print N "\n";

    # The flag masks for individual bits

    $next = 0;
    for ($i = 0; $i < $n_iflags; $i++) {
	if (($i & 31) == 0) {
	    printf N "/* Mask bits for field %d : %d...%d */\n",
		$i >> 5, $i, $i+31;
	}
	if (defined(my $v = $flag_bynum[$i])) {
	    printf N "#define IFM_%-15s UINT32_C(0x%08x)     /* %3d */\n",
		$v->[1], 1 << ($i & 31), $i;
	    $next = $i+1;
	}
    }
    print N "\n";

    # The names of fields

    for ($i = 0; $i <= $#flag_fields; $i++) {
	printf N "#define %-19s %3d /* %-64s */\n",
	    'IF_'.$flag_fields[$i]->[0].'_FIELD',
	    $flag_fields[$i]->[1] >> 5,
	    sprintf("IF_%s (%d) ... IF_%s (%d)",
		    $flag_bynum[$flag_fields[$i]->[1]]->[1],
		    $flag_bynum[$flag_fields[$i]->[1]]->[0],
		    $flag_bynum[$flag_fields[$i]->[2]]->[1],
		    $flag_bynum[$flag_fields[$i]->[2]]->[0]);
	printf N "#define %-19s %3d\n",
	    'IF_'.$flag_fields[$i]->[0].'_NFIELDS',
	    ($flag_fields[$i]->[2] - $flag_fields[$i]->[1] + 31) >> 5;
    }
    print N "\n";

    printf N "#define IF_FIELD_COUNT %d\n", $iflag_words;
    print N "typedef struct {\n";
    print N "    uint32_t field[IF_FIELD_COUNT];\n";
    print N "} iflag_t;\n";

    print N "\n";
    print N "/* All combinations of instruction flags used in instruction patterns */\n";
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
    print N "/* All combinations of instruction flags used in instruction patterns */\n";
    printf N "const iflag_t insns_flags[%d] = {\n",
        $#insns_flag_values + 1;
    foreach my $i (0 .. $#insns_flag_values) {
        printf N "    {{%s}}, /* %3d : %s */\n",
	    $insns_flag_values[$i], $i, $insns_flag_lists[$i];
    }
    print N "};\n";
    close N;
}

1;
