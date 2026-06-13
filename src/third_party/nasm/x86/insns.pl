#!/usr/bin/perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright 1996-2025 The NASM Authors - All Rights Reserved

#
# insns.pl
#
# Parse insns.dat and produce generated source code files
#
# See x86/bytecode.txt for the defintion of the byte code
# output to x86/insnsb.c.
#

require 'x86/insns-iflags.ph';

# Create disassembly root tables
my @vex_class = ( 'novex', 'vex', 'xop', 'evex', 'rex2' );
my $vex_classes = scalar(@vex_class);
my @map_count  = ( 4, 32, 24, 8, 2 );
my @map_start = ( 0, 0, 8, 0, 0 );
my @map_max;
for ($c = 0; $c < $vex_classes; $c++) {
    push(@distable, [(undef) x $map_start[$c]]);
    push(@map_max, $map_start[$c] + $map_count[$c] - 1);
    for ($m = $map_start[$c]; $m <= $map_max[$c]; $m++) {
	push(@{$distable[$c]}, {});
    }
}
@disasm_prefixes = (@vexlist, @disasm_prefixes, '');
%disasm_prefixes = map { $_ => 1 } @disasm_prefixes;

@bytecode_count = (0) x 256;

# Push to an array reference, creating the array if needed
sub xpush($@) {
    my $ref = shift @_;

    $$ref = [] unless (defined($$ref));
    return push(@$$ref, @_);
}

#
# Here we determine the set of possible [encoding, map, opcode] sets
# for a given instruction, used to generate the disassembler tables.
#
# It is only necessary to take into account the following byte codes:
# \[1234]      mean literal bytes, of course
# \1[0123]     mean byte plus register value
# \30[0123]    0F 1[8-F]
# \35[01]      REX2 prefix
# \35[567]     legacy map number
# \26x \270    VEX prefix and map
# \24x \250    EVEX prefix and map
# prefixes     ignored
#
my @skip_bytecode = ((0) x 256);
map { $skip_bytecode[$_] = 1; }
(05...07, 014...017, 0271...0273, 0310...0337,
 0341...0347, 0360...0372, 0374...0376);

# To assist with debugging...
my %skipped_due_to;

# Create combinatorial sets of possible encodings
sub genseqs($$$@) {
    my($flags, $enc, $map, @opcodes) = @_;
    my @encs = ($enc);

    if ($enc == 0 && $map < 2 && $flags !~ /\bNO(?:APX|REX)\b/) {
	push(@encs, 4);		# Allow REX2 encoding
    }

    my @seqs = ();
    foreach my $enc (@encs) {
	push(@seqs, map { [$enc, $map, $_] } @opcodes);
    }

    return @seqs;
}

sub startseq($$) {
    my ($codestr,$flags) = @_;
    my @codes = ();
    my $c = $codestr;
    my($c0, $c1, $i);
    my $prefix = '';
    my $enc = 0;		# Legacy
    my $map = 0;		# Map 0

    @codes = decodify(undef, $codestr, {}, undef);

    while (defined($c0 = shift(@codes))) {
        $c1 = $codes[0];	# The immediate following code
        if ($c0 >= 01 && $c0 <= 04) {
            # Fixed byte string, this should be the opcode
	    return genseqs($flags, $enc, $map, $c1);
        } elsif ($c0 >= 010 && $c0 <= 013) {
            return genseqs($flags, $enc, $map, $c1...($c1+7));
        } elsif (($c0 & ~013) == 0144) {
            return genseqs($flags, $enc, $map, $c1, $c1|2);
	} elsif (($c0 & ~3) == 0300) {
	    return genseqs($flags, $enc, $map, 0x18...0x1f);
	} elsif ($c0 >= 0355 && $c0 <= 0357) {
	    $map = $c0 - 0354;
	} elsif (($c0 & ~1) == 0350) {
	    # REX2
	    $enc = 4;		# rex2 required
        } elsif (($c0 & ~3) == 0260 || $c0 == 0270) {
	    # VEX/XOP
            my($cm,$wlp);
            $cm  = shift(@codes);
            $wlp = shift(@codes);
            $enc = (($cm >> 6) & 1) + 1; # vex or xop
            $map = $cm & 31;
	} elsif (($c0 & ~3) == 0240 || $c0 == 0250) {
	    # EVEX
	    my @p;
	    push(@p, shift(@codes));
	    push(@p, shift(@codes));
	    push(@p, shift(@codes));
	    my $tuple = shift(@codes);
	    $map = $p[0] & 7;
	    $enc = 3;	# evex
	} elsif (!$skip_bytecode[$c0]) {
	    # This cannot be an opcode
	    $skipped_due_to{$c0}++;
	    last;
	}
    }
    return ();
}

# Generate relaxed form patterns if applicable
# * is used for an optional source operand, duplicating the previous one
#   in the encoding if it is missing.
# ? is used for an optional destination operand which is not encoded if
#   missing; if combined with evex.ndx set the nd bit if present.
sub relaxed_forms(@) {
    my @field_list = ();

    foreach my $fields (@_) {
	if ($fields->[1] !~ /([\*\?])/) {
	    push(@field_list, $fields);
	    next;
	}
	my $flag = $1;

	# This instruction has relaxed form(s)
	if ($fields->[2] !~ /^(\[\s*)([^:\s]+)(\s*:.*\])$/) {
	    warn "$fname:$line: has a $flag operand but uses raw bytecodes\n";
	    push(@field_list, $fields);
	    next;
	}
	# Subfields of the instruction encoding field;
	# [1] is the encoding of the operands
	my @f2o = ($1, $2, $3);

	my $opmask = 0;
	my $ndmask = 0;
	my $ndflag = 0;
	my @ops = split(/,/, $fields->[1]);
	for (my $oi = 0; $oi < scalar @ops; $oi++) {
	    if ($ops[$oi] =~ /\*$/) {
		if ($oi == 0) {
		    warn "$fname:$line: has a first operand with a *\n";
		    next;
		}
		$opmask |= 1 << $oi;
	    } elsif ($ops[$oi] =~ /\?$/) {
		$opmask |= 1 << $oi;
		$ndmask |= 1 << $oi;
		$ndflag = 1;
	    }
	}

	# If .ndx is present, then change it to .nd0 or .nd1
	# Set to .nd0 if no ndmask fields are present, otherwise 1
	# (This line applies to the full form instruction; see below
	# for the modified versions.)
	my $has_ndx = ($f2o[2] =~ /(\.nd)x\b/);

	my %flags = split_flags($fields->[3]);
	my $has_zu  = $flags{'ZU'};

	for (my $oi = 0; $oi < (1 << scalar @ops); $oi++) {
	    if (($oi & ~$opmask) == 0) {
		# First and ending variants. 0 means a ? operand is to
		# be omitted entirely, a 1 that is is to be treated
		# like the *next* operand (emitted with a relax annotation.)
		# This is used to generate the {zu} forms of instrutions
		# which support ND - the {zu} form is created as the ND form
		# with the destination and first source operand the same.
		my $fvar = !($has_ndx && ($oi & $ndmask));
		my $evar = $fvar || ($has_ndx && !$has_zu);
		for (my $var = $fvar; $var <= $evar; $var++) {
		    my @xops = ();
		    my $ndflag = 0;
		    my $ondflag = 0;
		    my $setzu = 0;
		    my $voi = $oi;
		    my $f2 = $f2o[0];
		    my $dropped = 0;
		    for (my $oj = 0; $oj < scalar(@ops); $oj++) {
			my $ob = 1 << $oj;
			my $emit = 0;
			if ($ob & ~$voi) {
			    $emit = 1;
			} elsif ($ob & $ndmask) {
			    die if (($ob << 1) & $ndmask);
			    if (!$var) {
				# Make it disappear completely
				$dropped |= $ob;
				next;
			    } else {
				# Treat the *next* operand as an omitted
				# * operand
				$emit = 1;
				$voi |= $ob << 1;
			    }
			}
			if ($emit) {
			    push(@xops, $ops[$oj]);
			    if ($ndmask & $ob) {
				$setzu |= $has_ndx;
				$ondflag = 1;
			    }
			} else {
			    $dropped |= $ob;
			    $f2 .= '+';
			}
			$f2 .= substr($f2o[1], $oj, 1);
		    }
		    $f2 .= $f2o[2];
		    my @ff = @$fields;
		    $ff[1] = join(',', @xops);
		    $f2 =~ s/(\.nd)x\b/$1$ondflag/ if ($has_ndx);
		    $ff[2] = $f2;
		    my %newfl = %flags;
		    $newfl{'ZU'}++ if ($setzu && !$has_zu);
		    $newfl{'ND'}++ if ($oi && $var);
		    # Change ARx and SMx if needed
		    foreach my $as ('AR', 'SM') {
			my $ndelta = 0;
			for (my $i = 0; $i < $MAX_OPERANDS; $i++) {
			    if ($dropped & (1 << $i)) {
				delete $newfl{"$as$i"};
				$ndelta++;
			    } elsif ($ndelta) {
				my $newi = $i - $ndelta;
				if ($newfl{"$as$i"}) {
				    delete $newfl{"$as$i"};
				    $newfl{"$as$newi"}++ if ($newi >= 0);
				}
			    }
			}
		    }

		    $ff[3] = merge_flags(\%newfl);

		    push(@field_list, [@ff]);
		}
	    }
	}
    }

    return @field_list;
}

# Condition codes.
my $c_ccmask = 0x0f;
my $c_nd     = 0x10;		# Not for the disassembler
my $c_cc     = 0x20;		# cc only (not scc)
my $c_scc    = 0x40;		# scc only (not cc)
my %conds = (
    'o'   =>  0,             'no'  =>  1,        'c'   =>  2,        'nc'  =>  3,
    'z'   =>  4,             'nz'  =>  5,        'na'  =>  6,        'a'   =>  7,
    's'   =>  8,             'ns'  =>  9,
    'pe'  => 10|$c_cc,       'po'  => 11|$c_cc,
    't'   => 10|$c_scc,      'f'   => 11|$c_scc,
    'l'   => 12,             'nl'  => 13,        'ng'  => 14,        'g'   => 15,

    'ae'  =>  3|$c_nd,       'b'   =>  2|$c_nd,  'be'  =>  6|$c_nd,  'e'   =>  4|$c_nd,
    'ge'  => 13|$c_nd,       'le'  => 14|$c_nd,  'nae' =>  2|$c_nd,  'nb'  =>  3|$c_nd,
    'nbe' =>  7|$c_nd,       'ne'  =>  5|$c_nd,  'nge' => 12|$c_nd,  'nle' => 15|$c_nd,
    'np'  => 11|$c_nd|$c_cc, 'p'   => 10|$c_nd|$c_cc,
    ''    => 11|$c_nd|$c_scc);

my @conds = sort keys(%conds);

# Generate conditional form patterns if applicable
sub conditional_forms(@) {
    my @field_list = ();

    foreach my $fields (@_) {
	# This is a case sensitive match!
	if ($fields->[0] !~ /s?cc/) {
	    # Not a conditional instruction pattern
	    push(@field_list, $fields);
	    next;
	}

	if ($fields->[2] !~ /^\[/) {
	    warn "$fname:$line: conditional instruction using raw bytecodes\n";
	    next;
	}

	my $exclude_mask = ($fields->[0] =~ /scc/ ? $c_cc : $c_scc);

	foreach my $cc (@conds) {
	    my $ccval = $conds{$cc};
	    next if ($ccval & $exclude_mask);

	    my @ff = @$fields;

	    $ff[0] =~ s/s?cc/\U$cc/;

	    if ($ff[2] =~ /^(\[.*?)\b([0-9a-f]{2})\+c\b(.*\])$/) {
		$ff[2] = $1.sprintf('%02x', hex($2)^($ccval & $c_ccmask)).$3;
	    } elsif ($ff[2] =~ /^(\[.*?\.scc\b)(.*\])$/) {
		$ff[2] = $1.sprintf('%d', $ccval & $c_ccmask).$2;
	    } else {
		my $eerr = $ff[2];
		warn "$fname:$line: invalid conditional encoding: $eerr\n";
		next;
	    }

	    if (($ccval & $c_nd) && !($ff[3] =~ /\bND\b/)) {
		$ff[3] .= ',ND';
	    }

	    push(@field_list, [@ff]);
	}
    }
    return @field_list;
}

# APX: EVEX promoted forms of VEX instructions
sub apx_evex_forms(@) {
    my @field_list;

    foreach my $fields (@_) {
	my $doapx = 1;
	my @ff = @$fields;

	$doapx = 0 if ($ff[3] =~ /\bNOAPX\b/);
	$doapx = 0 if ($ff[2] !~ /^(\[.*?)\b(vex\+)(\.[^\s]*)?(.*\])$/);

	if (!$doapx) {
	    push(@field_list, $fields);
	    next;
	}

	my($head,$vexplus,$vexargs,$tail) = ($1,$2,$3,$4);

	$doapx = $vexargs !~ /\.m([89|[1-9][0-9]+)\b/;

	$ff[2] = $head.'vex'.$vexargs.$tail;
	push(@field_list, [@ff]);
	if ($doapx) {
	    $ff[2] = $head.'evex'.$vexargs.$tail;
	    $ff[3] .= ',APX';
	    push(@field_list, [@ff]);
	}
    }

    return @field_list;
}

print STDERR "Reading insns.dat...\n";

@args   = ();
undef $output;
foreach $arg ( @ARGV ) {
    if ( $arg =~ /^\-/ ) {
        if  ( $arg =~ /^\-([abdin]|f[hc])$/ ) {
            $output = $1;
        } else {
            die "$0: Unknown option: ${arg}\n";
        }
    } else {
        push (@args, $arg);
    }
}

die if (scalar(@args) != 2);	# input output
($fname, $oname) = @args;

open(F, '<', $fname) || die "unable to open $fname";

@bytecode_list = ();
%aname = ();

$line = 0;
$insns = 0;
$n_opcodes = 0;
my @allpatterns = ();

while (<F>) {
    $line++;
    chomp;
    next if ( /^\s*(\;.*|)$/ );   # comments or blank lines

    unless (/^\s*(\S+)\s+(\S+)\s+(\S+|\[.*\])\s+(\S+)\s*$/) {
        warn "$fname:$line: line does not contain four fields\n";
        next;
    }
    my @field_list = ([$1, $2, $3, uc($4)]);
    @field_list = relaxed_forms(@field_list);
    @field_list = conditional_forms(@field_list);
    @field_list = apx_evex_forms(@field_list);

    foreach my $fields (@field_list) {
        ($formatted, $nd) = format_insn(@$fields);
        if ($formatted) {
            $insns++;
	    xpush(\$aname{$fields->[0]}, [$formatted, $fields]);
        }
	if (!defined($k_opcodes{$fields->[0]})) {
	    $k_opcodes{$fields->[0]} = $n_opcodes++;
	}
        if ($formatted && !$nd) {
            push(@big, [$formatted, $fields]);
            my @sseq = startseq($fields->[2], $fields->[3]);
            foreach my $i (@sseq) {
		xpush(\$distable[$i->[0]][$i->[1]]{$i->[2]}, $#big);
            }
        }
    }
}

close F;

#
# Generate the bytecode array.  At this point, @bytecode_list contains
# the full set of bytecodes.
#

# Sort by descending length
@bytecode_list = sort { scalar(@$b) <=> scalar(@$a) } @bytecode_list;
@bytecode_array = ();
%bytecode_pos = ();
$bytecode_next = 0;
foreach $bl (@bytecode_list) {
    my $h = hexstr(@$bl);
    next if (defined($bytecode_pos{$h}));

    push(@bytecode_array, $bl);
    while ($h ne '') {
        $bytecode_pos{$h} = $bytecode_next;
        $h = substr($h, 2);
        $bytecode_next++;
    }
}
undef @bytecode_list;

@opcodes = sort { $k_opcodes{$a} <=> $k_opcodes{$b} } keys(%k_opcodes);

if ( $output eq 'b') {
    print STDERR "Writing $oname...\n";

    open(B, '>', $oname);

    print B "/* This file auto-generated from insns.dat by insns.pl" .
        " - don't edit it */\n\n";

    print B "#include \"nasm.h\"\n";
    print B "#include \"insns.h\"\n\n";

    print B "const uint8_t nasm_bytecodes[$bytecode_next] = {\n";

    $p = 0;
    foreach $bl (@bytecode_array) {
        printf B "    /* %5d */ ", $p;
        foreach $d (@$bl) {
            printf B "%#o,", $d;
            $p++;
        }
        printf B "\n";
    }
    print B "};\n";

    print B "\n";
    print B "/*\n";
    print B " * Bytecode frequencies (including reuse):\n";
    print B " *\n";
    for ($i = 0; $i < 32; $i++) {
        print B " *";
        for ($j = 0; $j < 256; $j += 32) {
            print B " |" if ($j);
            printf B " %3o:%5d", $i+$j, $bytecode_count[$i+$j];
        }
        print B "\n";
    }
    print B " */\n";

    close B;
}

if ( $output eq 'a' ) {
    print STDERR "Writing $oname...\n";

    open(A, '>', $oname);

    print A "/* This file auto-generated from insns.dat by insns.pl" .
        " - don't edit it */\n\n";

    print A "#include \"nasm.h\"\n";
    print A "#include \"insns.h\"\n\n";

    foreach my $i (@opcodes) {
	my $pat = $aname{$i};
	if (scalar(@$pat)) {
	    printf A "static const struct itemplate instrux_%s[%d] = {\n",
		$i, scalar(@$pat);
	    my $e = '}';
	    my $n = 0;
	    foreach $j (@$pat) {
		printf A "    /* %3d : %s */\n", $n++, join(' ', @{$j->[1]});
		print A '        /* ', show_bytecodes($j->[0]), ' : ', show_iflags($j->[0]), " */\n";
		print A '        ', codesubst($j->[0]), "\n";
	    }
	    print A "};\n\n";
	}
    }
    printf A "const struct itemplate_list nasm_instructions[%d] = {\n",
	scalar(@opcodes);
    foreach my $i (@opcodes) {
	my $pat  = $aname{$i};
	my $npat = scalar(@$pat);
	my $tbl  = $npat ? "instrux_$i" : "NULL /* $i */";
	printf A "    { %3d, %s },\n", $npat, $tbl;
    }
    print A "};\n";

    close A;
}

if ( $output eq 'd' ) {
    print STDERR "Writing $oname...\n";

    open(D, '>', $oname);

    print D "/* This file auto-generated from insns.dat by insns.pl" .
        " - don't edit it */\n\n";

    my @skipped = sort { $a <=> $b } keys(%skipped_due_to);

    if (scalar @skipped) {
	print D "/*\n";
	print D " * Note: skipped patterns due to byte codes:\n";
	print D " *", map { sprintf(' 0%o', $_) } @skipped;
	print D "\n */\n\n";
    }

    print D "#include \"nasm.h\"\n";
    print D "#include \"insns.h\"\n\n";

    print D "static const struct itemplate instrux[] = {\n";
    $n = 0;
    foreach $j (@big) {
	printf D "    /* %3d : %s */\n", $n++, join(' ', @{$j->[1]});
	print D '        /* ', show_bytecodes($j->[0]), ' : ', show_iflags($j->[0]), " */\n";
	print D '        ', codesubst($j->[0]), "\n";
    }
    print D "};\n";

    my @dinstname;
    for (my $c = 0; $c < $vex_classes; $c++) {
	push(@dinstname, []);
	for (my $m = $map_base[$c]; $m <= $map_max[$c]; $m++) {
	    my $ninst = scalar(keys %{$distable[$c][$m]});
	    if (!$ninst) {
		push(@{$dinstname[$c]}, 'NULL');
	    } else {
		my $tname = sprintf("itbl_%s_map%d", $vex_class[$c], $m);
		push(@{$dinstname[$c]}, $tname);
		my @itbls = ();
		for (my $o = 0; $o < 256; $o++) {
		    my $tbl = $distable[$c][$m]{$o};
		    if (defined($tbl)) {
			my $name = sprintf("%s_%02x", $tname, $o);
			push(@itbls, $name);
			printf D "\nstatic const struct itemplate * const %s[] = {\n", $name;
			foreach my $j (@$tbl) {
			    print D "    instrux + $j,\n";
			}
			print D "    NULL\n};\n";
		    } else {
			push(@itbls, 'NULL');
		    }
		}

		printf D "\nstatic const struct itemplate * const * const %s[256] = {\n", $tname;
		print D map { "    $_,\n" } @itbls;
		print D "};\n";
	    }
        }
    }

    print D "\nconst struct itemplate * const * const * const\n";
    print D "ndisasm_itable[] = {\n";
    for (my $c = 0; $c < $vex_classes; $c++) {
	my $class = $vex_class[$c];
	printf D "    /* ---- %s ---- */\n", $class;
	for (my $m = $map_start[$c]; $m <= $map_max[$c]; $m++) {
	    printf D "    /* %2d */ %s,\n", $m, $dinstname[$c][$m];
	}
    }
    print D "};\n";

    close D;
}

if ( $output eq 'i' ) {
    print STDERR "Writing $oname...\n";

    open(I, '>', $oname);

    print I "/* This file is auto-generated from insns.dat by insns.pl" .
        " - don't edit it */\n\n";
    print I "/* This file in included by nasm.h */\n\n";

    print I "/* Instruction names */\n\n";
    print I "#ifndef NASM_INSNSI_H\n";
    print I "#define NASM_INSNSI_H 1\n\n";
    print I "enum opcode {\n";
    $maxlen = 0;
    foreach $i (@opcodes) {
        print I "\tI_${i},\n";
        $len = length($i);
        $maxlen = $len if ( $len > $maxlen );
    }
    print I "\tI_none = -1\n";
    print I "};\n\n";
    print I "#define MAX_INSLEN ", $maxlen, "\n";
    print I "#define MAX_OPERANDS ", $MAX_OPERANDS, "\n";
    print I "#define NASM_VEX_CLASSES ", $vex_classes, "\n";
    my $mapcnt = 0;
    for (my $c = 0; $c < $vex_classes; $c++) {
	printf I "#define MAP_BASE_%s (%d-%d)\n",
	    uc($vex_class[$c]), $mapcnt, $map_start[$c];
	$mapcnt += $map_count[$c];
    }

    print I "#define NO_DECORATOR\t{", join(',',(0) x $MAX_OPERANDS), "}\n";
    print I "\n#endif /* NASM_INSNSI_H */\n";

    close I;
}

if ( $output eq 'n' ) {
    print STDERR "Writing $oname...\n";

    open(N, '>', $oname);

    print N "/* This file is auto-generated from insns.dat by insns.pl" .
        " - don't edit it */\n\n";
    print N "#include \"tables.h\"\n\n";

    print N "const char * const nasm_insn_names[] = {";
    foreach $i (@opcodes) {
        print N "\n\t\"\L$i\"";
	print N ',' if ($i < $#opcodes);
    }
    print N "\n};\n";
    close N;
}

if ( $output eq 'fh') {
    write_iflaggen_h();
}

if ( $output eq 'fc') {
    write_iflag_c();
}

printf STDERR "Done: %d instructions\n", $insns;

# Count primary bytecodes, for statistics
sub count_bytecodes(@) {
    my $skip = 0;
    foreach my $bc (@_) {
        if ($skip) {
            $skip--;
            next;
        }
        $bytecode_count[$bc]++;
        if ($bc >= 01 && $bc <= 04) {
            $skip = $bc;
        } elsif (($bc & ~03) == 010) {
            $skip = 1;
        } elsif (($bc & ~013) == 0144) {
            $skip = 1;
        } elsif ($bc >= 0171 && $bc <= 0173) {
            $skip = 1;
        } elsif (($bc & ~3) == 0260 || $bc == 0270) {   # VEX
            $skip = 2;
        } elsif (($bc & ~3) == 0240 || $bc == 0250) {   # EVEX
            $skip = 4;
	} elsif (($bc & ~3) == 0304) {
	    $skip = 2;
        } elsif ($bc == 0330) {
            $skip = 1;
        }
    }
}

sub format_insn($$$$) {
    my ($opcode, $operands, $codes, $flags) = @_;
    my $nd = 0;
    my ($num, $flagsindex);
    my @bytecode;
    my ($op, @ops, @opsize, $opp, @opx, @oppx, @decos, @opevex);
    my $opinfo;

    return (undef, undef) if $operands eq 'ignore';

    # Remember if we have an ARx flag
    my $arx = undef;

    my %flags = split_flags($flags);
    set_implied_flags(\%flags);

    # Generate byte code. This may modify the flags.
    @bytecode = (decodify($opcode, $codes, \%flags, \$opinfo), 0);
    my($oppos, $openc) = @$opinfo;
    push(@bytecode_list, [@bytecode]);
    $codes = hexstr(@bytecode);
    count_bytecodes(@bytecode);

    my $nd = !!($flags{'ND'} || $flags{'PSEUDO'});
    delete $flags{'ND'};

    # format the operands
    $operands =~ s/[\*\?]//g;
    $operands =~ s/:/|colon,/g;
    @ops = ();
    @opsize = ();
    @decos = ();
    if ($operands ne 'void') {
        foreach $op (split(/,/, $operands)) {
	    my $opnum = scalar(@ops);
	    my $isreg = 0;
	    my $ismem = 0;
	    my $ismoffs = 0;
	    my $isimm = 0;
	    my $isrm  = 0;
	    my $iszero = 0;
	    my $opsz = 0;
            @opx = ();
            @opevex = ();
            foreach $opp (split(/\|/, $op)) {
                @oppx = ();
                if ($opp =~ s/^(b(16|32|64)|mask|z|er|sae)$//) {
                    push(@opevex, $1);
                }

		$opp =~ s/^reg([0-9]*)na$/reg_na$1/;

                if ($opp =~ s/([^0-9]0?)(8|16|32|64|80|128|256|512|1024|1k)$/$1/) {
                    push(@oppx, "bits$2");
		    $opsz = $1 + 0;
                }
		if ($opp =~ s/0$//) {
		    push(@oppx, 'rn_zero');
		    $iszero = 1;
		    if ($opp !~ /reg/) {
			$opp .= 'reg';
		    }
		}

                $opp =~ s/^memory_offs$/mem_offs/;
		$opp =~ s/^mem$/memory/;

		if ($opp =~ s/^(spec|imm)4$/$1/) {
		    push(@oppx, 'fourbits');
		    $isimm = 1;
		}
		$opp =~ s/^spec$/immediate/; # Special or normal immediate
		$opp =~ s/^imm$/imm_normal/; # Normal immediate only
		if ($opp =~ /^(unity|sbyted?word|[su]dword)$/) {
		    push(@oppx, 'imm_normal');
		    $isimm = 1;
		}
		if ($opp =~ /^imm/) {
		    $isimm = 1;
		}
                $opp =~ s/^([a-z]+)rm$/rm_$1/;
                $opp =~ s/^(rm|reg)$/$1_gpr/;
		$opp =~ s/^rm_k$/rm_opmask/;
		$opp =~ s/^kreg$/opmaskreg/;
		if ($opp =~ /\brm_/) {
		    $isrm = 1;
		} elsif ($opp =~ /(\breg_|reg\b)/) {
		    $isreg = 1;
		} elsif ($opp =~ /\b[xyzt]?mem/) {
		    $ismem = 1;
		}
		if ($opp =~ /\bmem_offs/) {
		    $ismoffs = 1;
		}
		if ($opp =~ /\b[xyzt]mm/) {
		    $isvec = 1;
		}
		if (($isrm || ($ismem && !$ismoffs) || $isreg) &&
		    !(($flags{'EVEX'} && $isvec) || !$flags{'NOAPX'})) {
		    # Register numbers >= 16 disallowed
		    push(@oppx, 'rn_l16');
		}
		if ($isreg && $isvec && $openc->[$opnum] =~ /b/) {
		    $flags{'MOPVEC'}++;
		}
                push(@opx, $opp, @oppx) if $opp;
            }

	    # Sanity-check the encoding of this operand
	    my $opvalid = '-';
	    if ($isreg) {
		$opvalid .= 'rvmsbx';
	    } elsif ($isimm || $ismoffs) {
		$opvalid .= 'ijnw';
	    } elsif ($ismem || $isrm) {
		$opvalid .= 'm';
	    }

	    foreach my $c (split(//, $openc->[$opnum])) {
		if (index($opvalid, $c) < 0) {
		    die "$fname:$line: $opcode: operand $opnum \"$op\": '$c' must be one of '$opvalid'\n";
		}
	    }

            $op = join('|',@opx);
            push(@ops, $op);
	    push(@opsize, $opsz);
            push(@decos, (@opevex ? join('|', @opevex) : '0'));
        }
    }

    my $nops = scalar(@ops);

    while (scalar(@ops) < $MAX_OPERANDS) {
        push(@ops, '0');
	push(@opsize, 0);
        push(@decos, '0');
    }
    $operands = join(',', @ops);
    $operands =~ tr/a-z/A-Z/;

    $decorators = "{" . join(',', @decos) . "}";
    if ($decorators =~ /^{(0,)+0}$/) {
        $decorators = "NO_DECORATOR";
    }
    $decorators =~ tr/a-z/A-Z/;

    # Tidy up the flags now then the operand count is known
    set_implied_flags(\%flags, $nops);

    # Look for SM flags clearly inconsistent with operand bitsizes
    my $ssize = 0;
    for (my $i = 0; $i < $nopr; $i++) {
	next if (!$flags{"SM$i"} || !$opsize[$i]);
	if (!$ssize) {
	    $ssize = $opsize[$i];
	} elsif ($opsize[$i] != $ssize) {
	    die "$fname:$line: $opcode: inconsistent SM flag for argument $i\n";
	}
    }

    # Look for ARx flags
    my $arx = opr_flags(\%flags, 'AR');

    # Look for Sx flags that can never match operand bitsizes. If the
    # intent is to never match (require explicit sizes), use the SX flag.
    # This doesn't apply to registers that pre-define specific sizes;
    # this should really be derived from include/opflags.h...
    my $fsize = size_flag(\%flags);
    if ($fsize) {
	for (my $i = 0; $i < $nops; $i++) {
	    next unless ($arx & (1 << $i));
	    if ($opsize[$i] && $ops[$i] !~ /(\breg_|reg\b)/ &&
		$opsize[$i] != $fsize) {
		die "$fname:$line: $opcode: inconsistent Sx flag for argument $i ($ops[$i])\n";
	    }
	}
    }

    # Generate the final index into the flags table
    $flagsindex = insns_flag_index(\%flags);
    die "$fname:$line: $opcode: error in flags $flags\n" unless (defined($flagsindex));

    return ("{I_$opcode, $nops, {$operands}, $decorators, \@\@CODES-$codes\@\@, $flagsindex},", $nd);
}

#
# Look for @@CODES-xxx@@ sequences and replace them with the appropriate
# offset into nasm_bytecodes
#
sub codesubst($) {
    my($s) = @_;
    my $n;

    while ($s =~ /\@\@CODES-([0-9A-F]+)\@\@/) {
        my $pos = $bytecode_pos{$1};
        if (!defined($pos)) {
            die "$fname:$line: no position assigned to byte code $1\n";
        }
        $s = $` . "nasm_bytecodes+${pos}" . "$'";
    }
    return $s;
}

#
# Extract byte codes in human-friendly form. Added as a comment
# to insnsa.c/insnsd.c to help debugging.
#
sub show_bytecodes($) {
    my($s) = @_;
    my @o = ();

    if ($s =~ /\@\@CODES-([0-9A-F]+)00\@\@/) {
	my $hstr = $1;
	my $literals = 0;
	my $hexlit = 0;
	for (my $i = 0; $i < length($hstr); $i += 2) {
	    my $c = hex(substr($hstr,$i,2));
	    if ($literals) {
		# Non-leading bytes
		$literals--;
		$o[-1] .= sprintf($hexlit ? '%02x' : '%03o', $c);
		$o[-1] .= $literals ? ' ' : ')';
	    } else {
		push(@o, sprintf("%03o", $c));
		if ($c <= 4) {
		    $literals = $c;
		    $hexlit = 1;
		} elsif (($c & ~3) == 0240 || $c == 0250) {
		    $literals = 3;
		    $hexlit = 1;
		} elsif (($c & ~3) == 0260 || $c == 0270) {
		    $literals = 2;
		    $hexlit = 0;
		} elsif ($c == 0171) {
		    $literals = 1;
		    $hexlit = 0;
		} elsif ($c == 0172) {
		    $literals = 1;
		    $hexlit = 1;
		}
		$o[-1] .= '(' if ($literals);
	    }
	}
	return join(' ', @o);
    } else {
	return 'no bytecode';
    }
}

# Get the iflags from an insnsa.c pattern.
# Added as a comment to help debugging.
sub show_iflags($) {
    my($s) = @_;
    return undef unless ($s =~ /([0-9]+)\},$/);
    return get_iflags($1);
}

#
# Turn a code string into a sequence of bytes
#
sub decodify($$$$) {
  # Although these are C-syntax strings, by convention they should have
  # only octal escapes (for directives) and hexadecimal escapes
  # (for verbatim bytes)
    my($opcode, $codestr, $flags, $opinfo) = @_;
    my @codes;

    if ($codestr eq 'ignore') {
	@codes = ();
    } elsif ($codestr =~ /^\s*\[([^\]]*)\]\s*$/) {
        @codes = byte_code_compile($opcode, $1, $flags, $opinfo);
    } else {
	# This really shouldn't happen anymore...
	warn "$fname:$line: raw bytecodes?!\n";

	my $c = $codestr;

	@codes = ();

	while ($c ne '') {
	    if ($c =~ /^\\x([0-9a-f]+)(.*)$/i) {
		push(@codes, hex $1);
		$c = $2;
		next;
	    } elsif ($c =~ /^\\([0-7]{1,3})(.*)$/) {
		push(@codes, oct $1);
		$c = $2;
		next;
	    } else {
		die "$fname:$line: unknown code format in \"$codestr\"\n";
	    }
	}
    }

    # Flags may have been updated
    set_implied_flags($flags);
    return @codes;
}

# Turn a numeric list into a hex string
sub hexstr(@) {
    my $s = '';
    my $c;

    foreach $c (@_) {
        $s .= sprintf("%02X", $c);
    }
    return $s;
}

# EVEX tuple types offset is 0300. e.g. 0301 is for full vector(fv).
sub tupletype($) {
    my ($tuplestr) = @_;
    my %tuple_codes = (
        ''      => 000,
        'fv'    => 001,
        'hv'    => 002,
        'fvm'   => 003,
        't1s8'  => 004,
        't1s16' => 005,
        't1s'   => 006,
        't1f32' => 007,
        't1f64' => 010,
        't2'    => 011,
        't4'    => 012,
        't8'    => 013,
        'hvm'   => 014,
        'qvm'   => 015,
        'ovm'   => 016,
        'm128'  => 017,
        'dup'   => 020,
    );

    if (defined $tuple_codes{$tuplestr}) {
        return 0300 + $tuple_codes{$tuplestr};
    } else {
        die "$fname:$line: undefined tuple type: $tuplestr\n";
    }
}

#
# This function takes a series of byte codes in a format which is more
# typical of the Intel documentation, and encode it.
#
# The format looks like:
#
# [operands: opcodes]
#
# The operands word lists the order of the operands:
#
# r = register field in the modr/m
# m = modr/m
# v = VEX "v" field or DFV
# i = immediate
# s = register field of is4/imz2 field
# - = implicit (unencoded) operand
# x = indeX register of mib. 014..017 bytecodes are used.
#
# For an operand that should be filled into more than one field,
# enter it as e.g. "r+v".
#
sub byte_code_compile($$$$) {
    my($opcode, $str, $flags, $opinfo) = @_;
    my $opr;
    my $opc;
    my @codes = ();
    my $litix = undef;
    my $i;
    my ($op, $oq);
    my $opex;

    my %imm_codes = (
        'ib'        => 020,     # imm8
        'ib,u'      => 024,     # Unsigned imm8
        'ib,s'      => 0274,    # imm8 sign-extended to opsize or bits
        'iw'        => 030,     # imm16
	'iw,s'      => 030,	# imm16 sign-extended: NOT IMPLEMENTED
	'iw,u'      => 030,	# imm16 zero-extended: NOT IMPLEMENTED
        'id'        => 040,     # imm32
        'id,s'      => 0254,    # imm32 sign-extended to opsize
	'id,u'      => 0264,	# imm32 zero-extended to opsize
        'iq'        => 054,	# imm64
	'iq,s'      => 054,	# for orthogonality
	'iq,u'      => 054,	# for orthogonality
        'iwd'       => 034,     # imm16 or imm32, depending on opsize
        'iwdq'      => 044,     # imm16/32/64, depending on addrsize
        'rel8'      => 050,
        'rel16'     => 060,
        'rel32'     => 070,
        'rel'       => 064,     # 16 or 32 bit relative operand
        'seg'       => 074,	# 16-bit segment value
	'ibn'       => 0300,	# A valid HINT_NOP opcode
    );
    my %plain_codes = (
	'o8'        => undef,   # 8-bit operand size (for orthogonality)
        'o16'       => 0320,    # 16-bit operand size
        'o32'       => 0321,    # 32-bit operand size
        'odf'       => 0322,    # Operand size is default
        'o64'       => 0324,    # 64-bit operand size requiring REX.W
	'w1'        => 0324,
        'a16'       => 0310,
        'a32'       => 0311,
        'adf'       => 0312,    # Address size is default (disassembly)
        'asz'       => 0312,    # Alias for adf, for macro convenience
	'asm'       => 0312,	# Alias for adf, for macro convenience
        'a64'       => 0313,
        '!osp'      => 0364,
        '!asp'      => 0365,
	'osp'       => 0366,
	'osz'       => 0330,	# Operand size depending on default
        'f2i'       => 0332,    # F2 prefix, but 66 for operand size is OK
        'f3i'       => 0333,    # F3 prefix, but 66 for operand size is OK
        'mustrep'   => 0336,
        'mustrepne' => 0337,
        'rex.l'     => 0334,
        'norexb'    => 0314,
        'norexx'    => 0315,
        'norexr'    => 0316,
        'norexw'    => 0317,
	'w0'        => 0317,
        'repe'      => 0335,
        'nohi'      => 0325,    # Use spl/bpl/sil/dil even without REX
        'nof3'      => 0326,    # No REP 0xF3 prefix permitted
        'norep'     => 0331,    # No REP prefix permitted
        'wait'      => 0341,    # Needs a wait prefix
	'osm'	    => 0342,	# Operand size = mode (16/32/64)
	'rex.b'	    => 0344,
	'rex.x'	    => 0345,
	'rex.r'	    => 0346,
	'rex.w'     => 0347,
        'resb'      => 0340,
        'np'        => 0360,    # No prefix
        'jlen'      => 0373,    # Length of jump
        'hlexr'     => 0271,
        'hlenl'     => 0272,
        'hle'       => 0273,

	'optw'      => 0336,
	'optd'      => 0337,

        # This instruction takes XMM VSIB
        'vsibx'     => 0374,
        'vm32x'     => 0374,
        'vm64x'     => 0374,

        # This instruction takes YMM VSIB
        'vsiby'     => 0375,
        'vm32y'     => 0375,
        'vm64y'     => 0375,

        # This instruction takes ZMM VSIB
        'vsibz'     => 0376,
        'vm32z'     => 0376,
        'vm64z'     => 0376,
    );

    unless ($str =~ /^(([^\s:]*)\:*([^\s:]*)\:|)\s*(.*\S)\s*$/) {
        die "$fname:$line: cannot parse: [$str]\n";
    }
    $opr = lc($2);
    $tuple = lc($3);    # Tuple type for AVX512
    $opc = lc($4);

    $op = 0;
    my $oppos = {};
    my $openc = [];
    if (defined($opinfo)) {
	$$opinfo = [$oppos, $openc];
    }
    for ($i = 0; $i < length($opr); $i++) {
        my $c = substr($opr,$i,1);
        if ($c eq '+') {
	    die "$fname:$line: $opcode: invalid use of '+' in '$opr'\n"
		if ($op < 1);
            $op--;
        } elsif ($c =~ /^[rmnvwsijbx-]$/) {
	    # n means an immediate which is encoded as a memory address,
	    # but unlike a mem_offs it supports rel encoding on 64 bits.
	    # w means an immediate to be encoded into the v register
	    # position.
	    (my $realc = $c) =~ tr/nw/mv/;
	    $openc->[$op] = '' unless (defined($openc->[$op]));
	    $openc->[$op] .= $c;
	    if (defined($oppos->{$realc})) {
		my $what = ($c eq $realc) ? "'$c'" : "[${realc}${c}]";
		die "$fname:$line: $opcode: More than one $what operand in '$opr'\n";
	    }
	    $oppos->{$realc} = $op unless ($realc eq '-');
	    $op++;
        } else {
	    die "$fname:$line: $opcode: Unknown operand encoding '$c'\n";
	}
    }

    if (defined($oppos->{'m'})) {
	if (defined($oppos->{'b'})) {
	    die "$fname:$line: $opcode: [mn] operand mutually exclusive with 'b'\n";
	} elsif (defined($oppos->{'x'})) {
	    # memory operand + x register operand requires MIB
	    $flags->{'MIB'}++;
	}
    }
    if (defined($oppos->{'s'}) && defined($oppos->{'i'})) {
	die "$fname:$line: $opcode: 's' operand mutually exclusive with 'i'\n";
    }
    if (defined($oppos->{'j'}) && !defined($oppos->{'i'})) {
	die "$fname:$line: $opcode 'j' without 'i' operand\n";
    }
    $tup = tupletype($tuple);

    # Opcode map. Map 0 is the default, but not explicitly defined yet.
    my $opmap = undef;

    my $last_imm = 'h';
    my $prefix_ok = 1;
    foreach $op (split(/\s*(?:\s|(?=[\/\\]))/, $opc)) {
        if (exists($plain_codes{$op})) {
            # Plain code
	    my $pc = $plain_codes{$op};
            push(@codes, $pc) if (defined($pc));
	} elsif ($op =~ /^(o64)?(nw)$/) {
	    push(@codes, $1 eq '' ? 0327 : 0323);
	    $flags->{'NWSIZE'}++;
        } elsif ($prefix_ok && $op =~ /^(66|f2|f3)$/) {
            # 66/F2/F3 prefix used as an opcode extension
            if ($op eq '66') {
                push(@codes, 0361);
            } elsif ($op eq 'f2') {
                push(@codes, 0332);
            } else {
                push(@codes, 0333);
            }
	} elsif ($prefix_ok && $op =~ /^(0f|0f38|0f3a|m([0-3]))$/) {
	    if ($2 ne '') {
		$opmap = $2 + 0;
	    } elsif ($op eq '0f') {
		$opmap = 1;
	    } elsif ($op eq '0f38') {
		$opmap = 2;
	    } elsif ($op eq '0f3a') {
		$opmap = 3;
	    }
	    push(@codes, 0354 + $opmap) if ($opmap > 0);
	    $prefix_ok = 0;
	} elsif ($op =~ /^(m[0-9]+|0f38|0f3a)$/) {
	    if ($prefix_ok) {
		die "$fname:$line: $opcode: invalid legacy map: $m\n";
	    } elsif (defined($opmap)) {
		die "$fname:$line: $opcode: multiple legacy map specifiers\n";
	    } else {
		die "$fname:$line: $opcode: legacy map must precede opcodes\n";
	    }
        } elsif ($op =~ /^[0-9a-f]{2}$/) {
            if (defined($litix) && $litix+$codes[$litix]+1 == scalar @codes &&
                $codes[$litix] < 4) {
                $codes[$litix]++;
                push(@codes, hex $op);
            } else {
                $litix = scalar(@codes);
                push(@codes, 01, hex $op);
            }
            $prefix_ok = 0;
        } elsif ($op eq '/r') {
            if (!defined($oppos->{'r'}) || !defined($oppos->{'m'})) {
                die "$fname:$line: $opcode: $op requires 'r' and [mn] operands\n";
            }
            $opex = (($oppos->{'m'} & 4) ? 06 : 0) |
                (($oppos->{'r'} & 4) ? 05 : 0);
            push(@codes, $opex) if ($opex);
            # if mib is composed with two separate operands - ICC style
            push(@codes, 014 + ($oppos->{'x'} & 3)) if (defined($oppos->{'x'}));
            push(@codes, 0100 + (($oppos->{'m'} & 3) << 3) + ($oppos->{'r'} & 3));
            $prefix_ok = 0;
        } elsif ($op =~ m:^/([0-7])$:) {
            if (!defined($oppos->{'m'})) {
                die "$fname:$line: $opcode: $op requires an [mn] operand\n";
            }
            push(@codes, 06) if ($oppos->{'m'} & 4);
            push(@codes, 0200 + (($oppos->{'m'} & 3) << 3) + $1);
            $prefix_ok = 0;
	} elsif ($op =~ m:^/([0-3]?)r([0-7])$:) {
	    if (!defined($oppos->{'r'})) {
                die "$fname:$line: $opcode: $op requires an 'r' operand\n";
	    }
	    push(@codes, 05) if ($oppos->{'r'} & 4);
	    push(@codes, 0171);
	    push(@codes, (($1+0) << 6) + (($oppos->{'r'} & 3) << 3) + $2);
	    $prefix_ok = 0;
        } elsif ($op =~ /^(vex|xop)(|\..*)$/) {
            my $vexname = $1;
            my $c = $vexname eq 'xop' ? 1 : 0;
            my ($m,$w,$l,$p) = (undef,undef,undef,undef);
            my $has_nds = 0;
            my @subops = split(/\./, $2);
	    my $opsize = undef;

	    if (defined($opmap)) {
		warn "$fname:$line: $opcode: legacy prefix ignored with VEX\n";
	    }
	    foreach $oq (@subops) {
		if ($oq eq '') {
		    next;
		} elsif ($oq eq '128' || $oq eq 'l0' || $oq eq 'lz') {
		    $l = 0;
		} elsif ($oq eq '256' || $oq eq 'l1') {
		    $l = 1;
		} elsif ($oq eq 'lig') {
		    $l = 0 unless (defined($l));
		    $flags->{'LIG'}++;
		} elsif ($oq eq 'w0') {
		    $w = 0;
		} elsif ($oq eq 'w1') {
		    $w = 1;
		} elsif ($oq eq 'wig') {
		    $w = 0;
		    $flags->{'WIG'}++;
		} elsif ($oq eq 'ww') {
		    $flags->{'WW'}++;
		} elsif ($oq eq 'o8') {
		    $p = 0 unless (defined($p)); # np
		    if (!defined($w)) {		 # wig
			$w = 0;
			$flags->{'WIG'}++;
		    }
		} elsif ($oq eq 'o16') {
		    $p = 1 unless (defined($p)); # 66
		    $w = 0 unless (defined($w)); # w0
		    $opsize = 0320;
		} elsif ($oq eq 'o32') {
		    $p = 0 unless (defined($p)); # np
		    if (!defined($w)) {
			$w = 0 unless (defined($w)); # w0
			$flags->{'WW'}++;
		    }
		    $opsize = 0321;
		} elsif ($oq eq 'o64') {
		    $p = 0 unless (defined($p)); # np
		    $w = 1 unless (defined($w)); # w1
		    $flags->{'WW'}++;
		    $opsize = 0323 + $w;
		} elsif ($oq eq 'ko8') {
		    $p = 1 unless (defined($p)); # 66
		    $w = 0 unless (defined($w)); # w0
		} elsif ($oq eq 'ko16') {
		    $p = 0 unless (defined($p)); # np
		    $w = 0 unless (defined($w)); # w0
		    # $opsize = 0320;
		} elsif ($oq eq 'ko32') {
		    $p = 1 unless (defined($p)); # 66
		    $w = 1 unless (defined($w)); # w1
		    # $opsize = 0321;
		} elsif ($oq eq 'ko64') {
		    $p = 0 unless (defined($p)); # 66
		    $w = 1 unless (defined($w)); # w1
		    # $opsize = 0323 + $w;
		} elsif ($oq eq 'np' || $oq eq 'p0') {
		    $p = 0;
		} elsif ($oq eq '66' || $oq eq 'p1') {
		    $p = 1;
		} elsif ($oq eq 'f3' || $oq eq 'p2') {
		    $p = 2;
		} elsif ($oq eq 'f2' || $oq eq 'p3') {
		    $p = 3;
		} elsif ($oq eq '0f') {
		    $m = 1;
		} elsif ($oq eq '0f38') {
		    $m = 2;
		} elsif ($oq eq '0f3a') {
		    $m = 3;
		} elsif ($oq =~ /^(m|map)([0-9]+)$/) {
		    $m = $2+0;
		} elsif ($oq eq 'nds' || $oq eq 'ndd' || $oq eq 'dds') {
		    if (!defined($oppos->{'v'})) {
			die "$fname:$line: $opcode: $vexname.$oq without [vw] operand\n";
		    }
		    $has_nds = 1;
		} else {
		    die "$fname:$line: $opcode: undefined modifier: $vexname.$oq\n";
		}
	    }
            if (!defined($m) || !defined($l)) {
                die "$fname:$line: $opcode: missing fields in \U$vexname\E specification\n";
            }

	    if (!defined($w)) {
		$w = 0;
		$flags->{'WIG'}++;
	    }

	    my $minmap = ($c == 1) ? 8 : 0; # 0-31 for VEX, 8-31 for XOP
	    if ($m < $minmap || $m > 31) {
		die "$fname:$line: $opcode: Only maps ${minmap}-31 are valid for \U${vexname}\n";
	    }
	    push(@codes, 05) if ($oppos->{'v'} > 3);
            push(@codes, defined($oppos->{'v'}) ? 0260+$oppos->{'v'} : 0270,
                 ($c << 6)+$m, ($w << 7)+($l << 2)+$p);

	    push(@codes, $opsize) if (defined($opsize));

	    $flags->{'VEX'}++;
	    $flags->{'NOAPX'}++; # VEX doesn't support registers 16+
            $prefix_ok = 0;
        } elsif ($op =~ /^(evex)(|\..*)$/) {
            my $c = $vexmap{$1};
            my ($m,$w,$l,$p,$scc,$nf,$u,$ndd) = (undef,undef,undef,undef,undef,undef,undef,0,0);
            my ($nds,$nd,$dfv,$v) = (0, 0, 0, 0);
	    my $opsize = undef;
	    my @bad_op = ();
            my @subops = split(/\./, $2);
	    if (defined($opmap)) {
		warn "$fname:$line: $opcode: legacy prefix ignored with EVEX\n";
	    }
	    foreach $oq (@subops) {
		if ($oq eq '') {
		    next;
		} elsif ($oq =~ /^(128|ll?[0z])$/) {
		    $l = 0;
		} elsif ($oq =~ /^(256|ll?1)$/) {
		    $l = 1;
		} elsif ($oq =~ /^(512|ll?2)$/) {
		    $l = 2;
		} elsif ($oq =~ /^(1024|1k|ll?3)$/) {
		    # Not actually defined, but...
		    $l = 3;
		} elsif ($oq eq 'lig') {
		    $l = 0 unless (defined($l));
		    $flags->{'LIG'}++;
		} elsif ($oq eq 'w0') {
		    $w = 0;
		} elsif ($oq eq 'w1') {
		    $w = 1;
		} elsif ($oq eq 'wig') {
		    $w = 0;
		    $flags->{'WIG'}++;
		} elsif ($oq eq 'ww') {
		    $w = 0;
		    $flags->{'WW'}++;
		} elsif ($oq eq 'np' || $oq eq 'p0') {
		    $p = 0;
		} elsif ($oq eq '66' || $oq eq 'p1') {
		    $p = 1;
		} elsif ($oq eq 'f3' || $oq eq 'p2') {
		    $p = 2;
		} elsif ($oq eq 'f2' || $oq eq 'p3') {
		    $p = 3;
		} elsif ($oq eq 'o8') {
		    $p = 0 unless (defined($p)); # np
		    if (!defined($w)) {		 # wig
			$w = 0;
			$flags->{'WIG'}++;
		    }
		} elsif ($oq eq 'o16') {
		    $p = 1 unless (defined($p)); # 66
		    $w = 0 unless (defined($w)); # w0
		    $opsize = 0320;
		} elsif ($oq eq 'o32') {
		    $p = 0 unless (defined($p)); # np
		    if (!defined($w)) {
			$w = 0 unless (defined($w)); # w0
			$flags->{'WW'}++;
		    }
		    $opsize = 0321;
		} elsif ($oq eq 'o64') {
		    $p = 0 unless (defined($p)); # np
		    $w = 1 unless (defined($w)); # w1
		    $flags->{'WW'}++;
		    $opsize = 0323 + $w;
		} elsif ($oq eq 'ko8') {
		    $p = 1 unless (defined($p)); # 66
		    $w = 0 unless (defined($w)); # w0
		} elsif ($oq eq 'ko16') {
		    $p = 0 unless (defined($p)); # np
		    $w = 0 unless (defined($w)); # w0
		    # $opsize = 0320;
		} elsif ($oq eq 'ko32') {
		    $p = 1 unless (defined($p)); # 66
		    $w = 1 unless (defined($w)); # w1
		    # $opsize = 0321;
		} elsif ($oq eq 'ko64') {
		    $p = 0 unless (defined($p)); # 66
		    $w = 1 unless (defined($w)); # w1
		    # $opsize = 0323 + $w;
		} elsif ($oq eq 'ww') {
		    $flags->{'WW'}++;
		} elsif ($oq eq '0f') {
		    $m = 1;
		} elsif ($oq eq '0f38') {
		    $m = 2;
		} elsif ($oq eq '0f3a') {
		    $m = 3;
		} elsif ($oq =~ /^(m|map)([0-7])$/) {
		    $m = $2+0;
		} elsif ($oq =~ /^scc([0-9]+)$/) {
		    $scc = $1+0;
		    $flags->{'SCC'}++;
		    push(@bad_op, ['v', $oq]);
		} elsif ($oq eq 'u') {
		    $u = 1;
		} elsif ($oq eq 'nf') {
		    $flags->{'NF_E'}++;
		    $nf = 0;
		} elsif ($oq =~ /^nf([01])$/) {
		    $nf = $1 + 0;
		} elsif ($oq =~ /^v([0-9]+)$/) {
		    $v = $1 + 0;
		    push(@bad_op, ['v', $oq]);
		} elsif ($oq eq 'dfv') {
		    $flags->{'DFV'}++;
		    $dfv = 1;
		    push(@bad_op, ['v', $oq]);
		} elsif ($oq =~ /^nd([01])$/) {
		    $nd = $1 + 0;
		} elsif ($oq eq 'zu') {
		    # Set .ND if {zu} prefix is present
		    $flags->{'ZU_E'}++;
		} elsif ($oq =~ /^(nds|ndd|nd|dds)$/) {
		    if (!defined($oppos->{'v'})) {
			die "$fname:$line: $opcode: evex.$oq without [vw] operand\n";
		    }
		    $nds = 1;
		    $nd  = $oq eq 'nd';
		} else {
		    die "$fname:$line: $opcode: undefined modifier: evex.$oq\n";
		}
	    }
	    # Currently too many patterns miss .w or .p; it would be good
	    # to figure out if they should be .[wp]0 or .[wp]ig, but
	    # don't warn yet to keep the noise down
            if (!defined($m) || !defined($l)) {
                warn "$fname:$line: $opcode: missing fields in EVEX specification\n";
            }
	    if ($m > 7) {
		die "$fname:$line: $opcode: Only maps 0-7 are valid for EVEX\n";
	    }
	    foreach my $bad (@bad_op) {
		my($what, $because) = @$inv;
		if (defined($oppos->{$what})) {
		    die "$fname:$line: $opcode: $what and evex.$because are mutually incompatible\n";
		}
	    }
	    if ($scc && $nf) {
		die "$fname:$line: $opcode: evex.scc and evex.nf are mutually incompatible\n";
	    }

	    my @p = ($m | 0xf0, $p | 0x7c, ($l << 5) | 0x08);
	    $v ^= 0x0f if ($dfv);
	    $v ^= 0x10 if (defined($scc));
	    $p[1] ^= ($v & 15) << 3;
	    $p[1] ^= $w << 7;
	    $p[2] ^= ($v & 16) >> 1;
	    $p[2] ^= $scc & 15;
	    $p[2] |= 0x04 if ($nf);
	    $p[2] |= 0x10 if ($nd);

	    push(@codes, 05) if ($oppos->{'v'} > 3);
	    push(@codes, defined($oppos->{'v'}) ? 0240+$oppos->{'v'} : 0250, @p);
	    push(@codes, $tup);

	    push(@codes, $opsize) if (defined($opsize));

	    $flags->{'EVEX'}++;
            $prefix_ok = 0;
	} elsif ($op =~ /^(rex2)(\..*)?$/) {
	    my $name = $1;
            my @subops = split(/\./, $2);
	    my $c = 0350;
	    my $m = undef;
	    foreach $oq (@subops) {
		if ($oq eq '') {
		    next;
		} elsif ($oq eq 'w') {
		    $c |= 01;
		} else {
		    die "$fname:$line: $opcode: unknown modifier: $name.$oq\n";
		}
	    }

	    push(@codes, $c);
	    $flags->{'APX'}++;
	    $flags->{'REX2'}++;
	    $flags->{'LONG'}++;
            $prefix_ok = 0;
        } elsif (defined $imm_codes{$op}) {
            if ($op eq 'seg') {
                if ($last_imm lt 'i') {
                    die "$fname:$line: $opcode: seg without an [ij] operand\n";
                }
            } else {
                $last_imm++;
                if ($last_imm gt 'j') {
                    die "$fname:$line: $opcode: too many immediate operands\n";
                }
            }
            if (!defined($oppos->{$last_imm})) {
                die "$fname:$line: $opcode: $op without '$last_imm' operand\n";
            }
            push(@codes, 05) if ($oppos->{$last_imm} & 4);
            push(@codes, $imm_codes{$op} + ($oppos->{$last_imm} & 3));
            $prefix_ok = 0;
	} elsif ($op =~ /^ib(,[us])?[\^]([0-9a-f]+)$/) {
	    my $type = $1 eq ',u' ? 2 : $1 eq ',s' ? 1 : 0;
	    my $mod  = hex $2;
	    $last_imm++;
	    if ($last_imm gt 'j') {
		die "$fname:$line: $opcode: too many immediate operands\n";
            }
	    if (!defined($oppos->{$last_imm})) {
		die "$fname:$line: $opcode: $op without '$last_imm' operand\n";
            }
	    push(@codes, 05) if ($oppos->{$last_imm} & 4);
	    push(@codes, 0304 + ($oppos->{$last_imm} & 3));
	    push(@codes, $type, $mod);
	    $flags->{'ND'}++;
        } elsif ($op eq '/is4') {
            if (!defined($oppos->{'s'})) {
                die "$fname:$line: $opcode: $op without 's' operand\n";
            }
            if (defined($oppos->{'i'})) {
                push(@codes, 0172, ($oppos->{'s'} << 3)+$oppos->{'i'});
            } else {
                push(@codes, 05) if ($oppos->{'s'} & 4);
                push(@codes, 0174+($oppos->{'s'} & 3));
            }
            $prefix_ok = 0;
        } elsif ($op =~ /^\/is4\=([0-9]+)$/) {
            my $imm = $1;
            if (!defined($oppos->{'s'})) {
                die "$fname:$line: $opcode: $op without 's' operand\n";
            }
            if ($imm < 0 || $imm > 15) {
                die "$fname:$line: $opcode: invalid imm4 value for $op: $imm\n";
            }
            push(@codes, 0173, ($oppos->{'s'} << 4) + $imm);
            $prefix_ok = 0;
        } elsif ($op =~ /^([0-9a-f]{2})\+r$/) {
            if (!defined($oppos->{'r'})) {
                die "$fname:$line: $opcode: $op without 'r' operand\n";
            }
            push(@codes, 05) if ($oppos->{'r'} & 4);
            push(@codes, 010 + ($oppos->{'r'} & 3), hex $1);
            $prefix_ok = 0;
	} elsif ($op =~ /^(jcc|jmp)8$/) {
	    # A relaxable jump instruction
	    push(@codes, $op eq 'jcc8' ? 0370 : 0371);
	    $flags->{'JMP_RELAX'}++;
        } elsif ($op =~ /^\\([0-7]+|x[0-9a-f]{2})$/) {
            # Escape to enter literal bytecodes
            push(@codes, oct $1);
        } else {
            die "$fname:$line: $opcode: unknown operation: $op\n";
        }
    }

    # Legacy maps 2+ do not support REX2 encodings
    if ($opmap > 1 && !$flags{'EVEX'}) {
	$flags->{'NOAPX'}++;
    }

    return @codes;
}
