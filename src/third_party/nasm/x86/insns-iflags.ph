#!/usr/bin/perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright 1996-2024 The NASM Authors - All Rights Reserved

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
my $no_word_break = 0;
my $current_group;
our $NOBREAK = 0;

# This should be generated automatically, really...
our $MAX_OPERANDS = 5;

sub if_($$) {
    my($name, $def) = @_;
    my $num = $n_iflags++;

    $name = uc($name);
    if (defined($flag_byname{$name})) {
	die "iflags: flag $name defined more than once\n";
    }

    my $v = [$num, $name, $def];

    if (!($n_iflags & 31) && $no_word_break) {
	die "iflags: group $current_group has disallowed dword break\n";
    }

    $flag_byname{$name}  = $v;
    $flag_bynum[$num] = $v;

    return 1;
}
sub if_break_ok(;$) {
    my($ok) = @_;
    $no_word_break = defined($ok) && !$ok;
}
sub if_align($;$) {
    my($name, $break_ok) = @_;

    if_break_ok($break_ok);

    if ($#flag_fields >= 0) {
	$flag_fields[$#flag_fields]->[2] = $n_iflags-1;
    }
    $n_iflags = ($n_iflags + 31) & ~31;

    if (defined($name)) {
	$current_group = $name;
	push(@flag_fields, [$name, $n_iflags, undef]);
    }

    return 1;
}

sub if_end() {
    if_align(undef);
    $iflag_words = $n_iflags >> 5;
}

# The actual flags definitions
require 'x86/iflags.ph';
if_end();

# Remove non-flags
sub clean_flags($) {
    my($flags) = @_;

    delete $flags->{''};
    delete $flags->{'0'};
    delete $flags->{'IGNORE'};
}

# Adjust flags which imply each other
sub set_implied_flags($;$) {
    my($flags, $oprs) = @_;
    $oprs = $MAX_OPERANDS unless (defined($oprs));

    clean_flags($flags);

    # If no ARx flags, make all operands ARx if a size is present
    # flag is present
    if (!opr_flags($flags, 'AR', $oprs)) {
	if (defined(size_flag($flags))) {
	    for (my $i = 0; $i < $oprs; $i++) {
		$flags->{"AR$i"}++;
	    }
	}
    }

    # Convert the SM flag to all possible SMx flags
    if ($flags->{'SM'}) {
	delete $flags->{'SM'};
	for (my $i = 0; $i < $oprs; $i++) {
	    $flags->{"SM$i"}++;
	}
    }

    # Delete SMx and ARx flags for nonexistent operands
    foreach my $as ('AR', 'SM') {
	for (my $i = $oprs; $i < $MAX_OPERANDS; $i++) {
	    delete $flags->{"$as$i"};
	}
    }

    $flags->{'LONG'}++     if ($flags->{'APX'});
    $flags->{'NOREX'}++    if ($flags->{'NOLONG'});
    $flags->{'NOAPX'}++    if ($flags->{'NOREX'});
    $flags->{'X86_64'}++   if ($flags->{'LONG'});
    $flags->{'PROT'}++     if ($flags->{'LONG'}); # LONG mode is a submode of PROT
    $flags->{'PROT'}++     if ($flags->{'EVEX'}); # EVEX not supported in real/v86 mode
    $flags->{'OBSOLETE'}++ if ($flags->{'NEVER'});
    $flags->{'NF'}++ if ($flags->{'NF_R'} || $flags->{'NF_E'});
    $flags->{'ZU'}++ if ($flags->{'ZU_R'} || $flags->{'ZU_E'});

    # Retain only the highest CPU level flag
    # CPU levels really need to be replaced with feature sets.
    my $found = $flags->{'PSEUDO'}; # Pseudo-ops don't have a CPU level
    for (my $i = $flag_byname{'ANY'}->[0]; $i >= $flag_byname{'8086'}->[0]; $i--) {
	my $f = $flag_bynum[$i]->[1];
	if ($found) {
	    delete $flags->{$f};
	} else {
	    $found = $flags->{$f};
	}
    }
    if (!$found) {
	# No CPU level flag at all; tag it FUTURE
	$flags->{'FUTURE'}++;
    }
}

# Return the value of any assume-size flag if one exists;
# SX, ANYSIZE or SIZE return 0 as they are size flags but
# don't have a known value at compile time.
sub size_flag($) {
    my %sflags = ( 'SB' => 8, 'SW' => 16, 'SD' => 32, 'SQ' => 64,
		   'ST' => 80, 'SO' => 128, 'SY' => 256, 'SZ' => 512,
		   'SX' => 0, 'OSIZE' => 0, 'ASIZE' => 0, 'ANYSIZE' => 0 );
    my($flags) = @_;

    foreach my $fl (keys(%sflags)) {
	if ($flags->{$fl}) {
	    return $sflags{$fl};
	}
    }

    return undef;
}

# Find any per-operand flags
sub opr_flags($$;$) {
    my($flags, $name, $oprs) = @_;
    $oprs = $MAX_OPERANDS unless (defined($oprs));
    my $nfl = 0;
    for (my $i = 0; $i < $oprs; $i++) {
	if ($flags->{"$name$i"}) {
	    $nfl |= 1 << $i;
	}
    }
    return $nfl;
}

# Split a flags field, returns a hash
sub split_flags($) {
    my($flagstr) = @_;
    my %flags = ();

    $flagstr = uc($flagstr);

    foreach my $flag (split(',', $flagstr)) {
	next if ($flag =~ /^\s*$/); # Null flag

	# Somewhat nicer syntax for required flags (NF! -> NF_R)
	$flag =~ s/\!$/_R/;
	# Ditto for weak flags (SX- -> SX_W)
	$flag =~ s/\-$/_W/;

	if ($flag =~ /^(.*)(([0-9]+[+-])+[0-9]+)$/) {
	    my $pref = $1;
	    my @rang = split(/([-+])/, "+$2");
	    shift(@rang);	# Drop empty entry at beginning
	    my $eor;
	    while (defined(my $sep = shift(@rang))) {
		my $nxt = shift(@rang) + 0;

		$eor = $nxt if ($sep eq '+');
		for (my $i = $eor; $i <= $nxt; $i++) {
		    $flags{"$pref$i"}++;
		}
		$eor = $nxt;
	    }
	} else {
	    $flags{$flag}++;
	}
    }

    clean_flags(\%flags);
    return %flags;
}

# Merge a flags field and strip flags with leading !
sub merge_flags($;$) {
    my($flags, $human) = @_;

    clean_flags(\%flags);

    my @flagslist = sort { $flag_byname{$a} <=> $flag_byname{$b} }
	grep { !/^(\s*|\!.*)$/ } keys(%$flags);

    if ($human) {
	# For possibe human consumption. Merge subsequent SM and AR
	# flags back into ranges.
	my @ofl = @flagslist;
	@flagslist = ();

	while (defined(my $fl = shift(@ofl))) {
	    if ($fl =~ /^(SM|AR)([0-9]+)$/) {
		my $pfx = $1;
		my $mask = 1 << $2;

		while ($ofl[0] =~ /^${pfx}([0-9]+)$/) {
		    $mask |= 1 << $1;
		    shift(@ofl);
		}

		my $n = 0;
		my $nstr;
		while ($mask) {
		    if ($mask & 1) {
			$nstr .= '+'.$n;
			if ($mask & 2) {
			    $nstr .= '-';
			    while ($mask & 2) {
				$n++;
				$mask >>= 1;
			    }
			    $nstr .= $n;
			}
		    }
		    $n++;
		    $mask >>= 1;
		}

		$nstr =~ s/^\+/$pfx/;
		push(@flagslist, $nstr);
	    } else {
		push(@flagslist, $fl);
	    }
	}
    }

    return scalar(@flagslist) ? join(',', @flagslist) : $human ? 'ignore' : '0';
}

# Compute the combinations of instruction flags actually used in templates

my %insns_flag_hash = ();
my @insns_flag_values = ();
my @insns_flag_lists = ();

sub insns_flag_index($) {
    my($flags) = @_;

    my $key = merge_flags($flags);
    my @prekey = sort(keys %$flags);

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

#
# Show the iflags corresponding to a specific iflags set extracted from
# a code sequence in human-readable form.
#
sub get_iflags($) {
    my($n) = @_;
    return $insns_flag_lists[$n];
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

    # The names of flag groups

    for ($i = 0; $i <= $#flag_fields; $i++) {
	printf N "/* IF_%s (%d) ... IF_%s (%d) */\n",
		    $flag_bynum[$flag_fields[$i]->[1]]->[1],
		    $flag_bynum[$flag_fields[$i]->[1]]->[0],
		    $flag_bynum[$flag_fields[$i]->[2]]->[1],
		    $flag_bynum[$flag_fields[$i]->[2]]->[0];

	# Bit definitions
	printf N "#define %-19s %3d\n",
	    'IF_'.$flag_fields[$i]->[0].'_FIRST',
	    $flag_fields[$i]->[1];
	printf N "#define %-19s %3d\n",
	    'IF_'.$flag_fields[$i]->[0].'_COUNT',
	    ($flag_fields[$i]->[2] - $flag_fields[$i]->[1] + 1);

	# Field (uint32) definitions
	printf N "#define %-19s %3d\n",
	    'IF_'.$flag_fields[$i]->[0].'_FIELD',
	    $flag_fields[$i]->[1] >> 5;
	printf N "#define %-19s %3d\n",
	    'IF_'.$flag_fields[$i]->[0].'_NFIELDS',
	    ($flag_fields[$i]->[2] - $flag_fields[$i]->[1] + 31) >> 5;
	print N "\n";
    }

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
