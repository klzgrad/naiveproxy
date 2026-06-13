#!/usr/bin/perl
#
# Preprocess the instruction pattern file.
#
# Generate some common repeated patterns here.
#
# Tag instructions which set the flags, and tag instructions
# which already do upper-zeroing.
#
# This can also be used to create other implied flags.
#

use integer;
use strict;

require 'x86/insns-iflags.ph';

our %macros;
our($macro, $outfile, $infile, $line);	# Public for error messages

# Common pattern for the basic 7 arithmetric functions
$macros{'arith'} = {
    'def' => *def_eightfold,
    'txt' => <<'EOL'
$$bwdq $op	rm#,reg#			[mr:	$hle o# $00# /r				]	8086,FL,SM,$lock
$$bwdq $op	reg#,rm#			[rm:	o# $02# /r				]	8086,FL,SM
$$wdq  $op	rm#,sbyte#			[mi:	$hle o# 83  /$n ib,s			]	8086,FL,SM,$lock
$$bwdq $op	ax#,imm#			[-i:	o# $04# i#				]	8086,FL,SM
$$bwdq $op	rm#,imm#			[mi:	$hle o# 80# /$n i#			]	8086,FL,SM,$lock
$$bwdq $op	reg#?,reg#,rm#			[vrm:	evex.ndx.$nf.l0.m4.o#     $02# /r	]	$evex,FL,APX,SM
$$bwdq $op	reg#?,rm#,reg#			[vmr:	evex.ndx.$nf.l0.m4.o#     $00# /r	]	$evex,FL,APX,SM
$$wdq  $op	reg#?,rm#,sbyte#		[vmi:	evex.ndx.$nf.l0.m4.o#     83 /$n ib,s	]	$evex,FL,APX,SM
$$bwdq $op	reg#?,rm#,imm#			[vmi:	evex.ndx.$nf.l0.m4.o#     80# /$n i#	]	$evex,FL,APX,SM
EOL
};

# Common pattern for the basic shift and rotate instructions
# Separate legacy and EVEX versions because additional patterns are
# needed to handle the -X VEX versions
$macros{'shift'} = {
    'def' => *def_eightfold,
	'txt' => <<'EOL'
$$bwdq $op	rm#,unity			[m-:	o# d0# /$n]				]	8086,FL
$$bwdq $op	rm#,reg_cl			[m-:	o# d2# /$n]				]	8086,FL
$$bwdq $op	rm#,reg_cx			[m-:	o# d2# /$n]				]	8086,FL,ND
$$bwdq $op	rm#,reg_ecx			[m-:	o# d2# /$n]				]	8086,FL,ND
$$bwdq $op	rm#,reg_rcx			[m-:	o# d2# /$n]				]	8086,FL,ND
$$bwdq $op	rm#,imm8			[mi:	o# c0# /$n ib,u]			]	186,FL
EOL
};

# APX EVEX versions
$macros{'eshift'} = {
    'def' => *def_eightfold,
	'txt' => <<'EOL'
$$bwdq $op	reg#?,rm#,unity			[vm-:	evex.ndx.nf.l0.m4.o#  d0# /$n		]	$apx,FL,SM0-1
$$bwdq $op	reg#?,rm#,reg_cl		[vm-:	evex.ndx.nf.l0.m4.o#  d2# /$n		]	$apx,FL,SM0-1
$$bwdq $op	reg#?,rm#,reg_cx		[vm-:	evex.ndx.nf.l0.m4.o#  d2# /$n		]	$apx,FL,SM0-1,ND
$$bwdq $op	reg#?,rm#,reg_ecx		[vm-:	evex.ndx.nf.l0.m4.o#  d2# /$n		]	$apx,FL,SM0-1,ND
$$bwdq $op	reg#?,rm#,reg_rcx		[vm-:	evex.ndx.nf.l0.m4.o#  d2# /$n		]	$apx,FL,SM0-1,ND
$$bwdq $op	reg#?,rm#,imm8			[vmi:	evex.ndx.nf.l0.m4.o#  c0# /$n ib,u	]	$apx,FL,SM0-1
EOL
};

# -X shifts
$macros{'xshift'} = {
    'func' => sub {
	my($mac, $args, $rawargs) = @_;
	my @ol;
	my $vex = 'vex';
	my $vfl = '';
	if (grep { /^evex=1$/ } @$rawargs) {
	    $vex = 'evex';
	    $vfl = 'APX';
	}
	foreach my $xf (['X',"$vfl"], ['', "$vfl,ND,NF!,OPT"]) {
	    my($x,$fl) = @$xf;
	    foreach my $os (32, 64) {
		my $w = ($os eq 32) ? 'w0' : 'w1';
		my $ixor = sprintf('%02x', $os-1);
		push(@ol, "ROR$x reg$os,rm$os,imm8       [rmi: $vex.lz.f2.0f3a.$w f0 /r ib] BMI2,SM0-1,!FL,$fl");
		push(@ol, "ROL$x reg$os,rm$os,imm_known8 [rmi: $vex.lz.f2.0f3a.$w f0 /r ib^$ixor] BMI2,SM0-1,!FL,$fl");
		foreach my $ss (8, 16, 32, 64) {
		    foreach my $opp (['SHL','66'], ['SAL','66'], ['SAR','f3'], ['SHR','f2']) {
			my($op,$pp) = @$opp;
			my $ndss = ',ND' unless ($ss == $os && $op ne 'SAL');
			push(@ol, "$op$x reg$os,rm${os}*,reg$ss [rmv: $vex.lz.$pp.0f38.$w f7 /r] BMI2,SM0-1,!FL,$fl,$ndss");
		    }
		}
	    }
	}
	return @ol;
    }
};

#
# Common pattern for multiple 32/64, 16/32/64, or 8/16/32/64 instructions.
# 'z' is used for a null-prefixed default-sized instruction (osm/osd)
#
my @sizename = ('z', 'b', 'w', 'd', 'q');

for (my $i = 1; $i <= 31; $i++) {
    my $n;
    for (my $j = 0; $j < scalar @sizename; $j++) {
	$n .= $sizename[$j] if ($i & (1 << $j));
    }
    $macros{$n} = { 'func' => *func_multisize, 'mask' => $i };
}

sub func_multisize($$$) {
    my($mac, $args, $rawargs) = @_;
    my @sbyte = ('imm8', 'imm8', 'sbyteword16', 'sbytedword32', 'sbytedword64');
    my $long = 0;		# 1 for LONG, 2 for NOLONG, 3 for invalid
    my $wwflag = 0;

    my @ol;
    my $mask = $mac->{'mask'};

    for (my $i = 0; $i < scalar(@sizename); $i++) {
	next unless ($mask & (1 << $i));
	my $s = ($i > 0) ? 4 << $i : '';
	my $sn = $sizename[$i];
	my $sz = $s || 'sm';
	my $o;
	my $ins = join(' ', @$rawargs);
	my $nd = 0;

	# Conditional pattern inclusions
	# Syntax: (which1:text1/which2:text2/text3)
	# ... where "which" is a combination of sizename letters.
	# No colon means unconditional ("else" clause)
	while ($ins =~ /^(.*?)\(([^\)]+)\)(.*)$/) {
	    $o .= $1;
	    my @cpp = split(/\//, $2);
	    $ins = $3;

	    my $found;
	    foreach my $cp (@cpp) {
		if ($cp =~ /^([a-z]+)\:(.*)$/) {
		    $cp = $2;
		    $found = $1 =~ /$sn/;
		} else {
		    $found = 1;
		}
		if ($found) {
		    $o .= $cp;
		    last;
		}
	    }
	}

	$ins = $o.$ins;
	$o = '';

	while ($ins =~ /^(.*?)((?:\b[0-9a-f]{2}(?:\+r)?|\bsbyte|\bimm|\bsel|\bopt\w?|\b[ioa]d?|\b(?:reg_)?[abcd]x|\bk?reg|\bk?rm|\bw|\bS\b)?\#{1,2}|\b(?:reg|rm)64\b|\b(?:o64)?nw\b|\b(?:NO)?LONG\w+\b|\%{1,2}|[ABCD]X\#)(.*)$/) {
	    $o .= $1;
	    my $mw = $2;
	    $ins = $3;
	    if ($mw eq '%') {
		$o .= uc($sn) if ($i);
	    } elsif ($mw eq '%%') {
		if ($i < 2) {
		    die "$0:$infile:$line: $mw cannot be used with z|b\n";
		}
		$o .= uc($sizename[$i-1]) . uc($sn);
	    } elsif ($mw =~ /^([0-9a-f]{2})(\+r)?(\#)?\#$/) {
		my $n;
		$n = ($3 ne '') ? $s > 16 : $s > 8;
		$n <<= 3 if ($2 ne '');
		$o .= sprintf('%02x%s', hex($1) | $n, $2);
	    } elsif ($mw eq 'sbyte#') {
		$o .= $sbyte[$i];
	    } elsif ($mw =~ /^imm\#(\#?)$/) {
		$o .= (($1 eq '' && $s >= 64) ? 'sdword' : 'imm').$s;
	    } elsif ($mw =~ '^([ao])(d?)\#$') {
		$o .= $1.("$2$sz" eq 'dsm' ? 'df' : $sz);
	    } elsif ($mw eq 'i#') {
		$o .= !$i ? 'iwd' : ($s >= 64) ? 'id,s' : "i$sn";
	    } elsif ($mw eq 'i##') {
		$o .= !$i ? 'iwdq' : "i$sn";
	    } elsif ($mw =~ /^(?:reg_)?([abcd])x\#$/i) {
		my $rl = $1;
		my $upr = ($rl =~ /^[A-Z]/);
		if ($i == 1) {
		    $o .= $upr ? "${rl}L" : "reg_${rl}l";
		} elsif ($i == 2) {
		    $o .= $upr ? "${rl}X" : "reg_${rl}x";
		} elsif ($i == 3) {
		    $o .= $upr ? "E${rl}X" : "reg_e${rl}x";
		} elsif ($i == 4) {
		    $o .= $upr ? "R${rl}X" : "reg_r${rl}x";
		    $long |= 1;
		} else {
		    die "$0:$infile:$line: register cannot be used with z\n";
		}
	    } elsif ($mw eq 'sel#') {
		# z is (ab)used to mean the memory form of a segment selector
		if ($i == 0) {
		    $o .= 'mem16';
		} elsif ($i == 1) {
		    die "$0:$infile:$line: $mw cannot be used with b\n";
		} else {
		    $o .= "reg$s";
		}
	    } elsif ($mw =~ /^opt(\w?)\#$/) {
		if ($i >= 2) {
		    my $opt .= $1 eq '' ? "opt$sn" : "opt$1";
		    if (!($opt eq 'optd' && $s == 16) && $opt ne 'optq') {
			$o .= $opt.' ';
		    }
		}
		$o .= "o$s" if ($s > 0);
	    } elsif ($mw =~ /^(o64)?nw$/) {
		$long |= 2 if ($i == 32); # nw = 32 bits not encodable in long mode
		$o .= $mw;
	    } elsif ($mw =~ /^((k?)(?:reg|rm))(\#{1,2}|[0-9]+)$/) {
		# (Possible) GPR reference
		$o     .= $1;
		my $isk = $2;
		my $n   = $3;
		if ($n eq '#') {
		    $n = $s;
		} elsif ($n eq '##') {
		    $n = $s >> 1;
		}
		$o .= $n;
		# 64-bit K registers OK in 32-bit mode
		$long |= 1 if ($n >= 64 && !$isk);
	    } elsif ($mw =~ /^(NO)?LONG(\w+)$/) {
		my $longflag = $1 ? 2 : 1;
		$long |= $longflag if ($2 =~ /$sn/i);
		# Drop
	    } elsif ($mw eq 'w#') {
		if (!$i) {
			$o .= 'ww';
			$wwflag = 1;
		} else {
			$o .= ($s >= 64) ? 'w1' : 'w0';
		}
	    } elsif ($mw eq 'w##') {
		$o .= 'w'.(($i-1) & 1);
	    } elsif ($mw eq 'S#') {
		$o .= 'S'
	    } elsif ($mw eq '#') {
		$o .= $s;
	    } else {
		die "$0:$infile:$line: unknown or invalid sequence \"$mw\"\n";
	    }
	}
	$o .= $ins;

	$o .= ',WW'     if ($wwflag);
	$o .= ',LONG'   if ($long & 1);
	$o .= ',NOLONG' if ($long & 2);
	$o .= ',ND'     if ($nd);

	if ($s >= 32 && $o !~ /\B\!386\b/ && $o =~ /\b(8086|[12]86)\b/) {
	    $o .= ',386';
	}

	push(@ol, $o);
    }

    return @ol;
}

# Near branch operand size patterns
# This allows the "normal" size patterns to be used for
# address size features, as used by JCXZ and LOOP.
# This also allows the syntax "jmp dword foo" in 64-bit
# mode, even though it is really bogus.
$macros{'br'} = {
    'func' =>
	sub {
	    my($mac, $args, $rawargs) = @_;
	    my @ol;
	    my $ins = join(' ', @$rawargs);

	    foreach my $wx ([16,16], [32,32], [64,64], [64,32]) {
		my($w,$iw,$sz) = @$wx;
		my $i = $ins;
		my $argn;
		if ($i =~ /^(.*)\b(near|short)\b/) {
		    my $what = $2;
		    next if ($what eq 'short' && $iw != $w);
		    (my $argn = $1) =~ s/[^,:]+//g;
		    $argn = 'AR'.length($argn);
		}
		$i =~ s/\b(near|short)\b/imm$iw|$1/;
		$i =~ s/\bos\b/nw o$w/;
		$i .= ",$argn";
		$i .= ($iw != $w) ? ',SX,ND' : ',OSIZE';
		$i .= ($w == 64) ? ',LONG' : ',NOLONG';
		push(@ol, $i);
	    }
	    return(@ol);
    }
};

# Common pattern for K-register instructions
$macros{'k'} = {
    'func' =>
	sub {
	    my($mac, $args, $rawargs) = @_;
	    my @ol;
	    my $ins = join(' ', @$rawargs);
	    my $xins;
	    my $n;

	    $ins .= ',ZU';
	    ($xins = $ins) =~ s/\bSM[0-9-]*,?//;
	    push(@ol, $xins);
	    ($xins = $ins) =~ s/\%//;
	    push(@ol, $xins);
	    if ($xins =~ s/\%//) {
		push(@ol, $xins);
	    }

	    # Allow instruction without K, as long as they aren't "TEST"
	    if ($ins !~ /^\bK\w*TEST/) {
		my @on;
		foreach my $oi (@ol) {
		    # Remove first capital K
		    ($xins = $oi) =~ s/\bK//;
		    push(@on, $xins);
		}
		push(@ol, @on);
	    }

	    # Allow SHIFT -> SH
	    if ($ins =~ /SHIFT/) {
		my @on;
		foreach my $oi (@ol) {
		    # Remove first capital K
		    ($xins = $oi) =~ s/SHIFT/SH/;
		    push(@on, $xins);
		}
	    push(@ol, @on);
	    }

	    # All instruction patterns except the first are ND
	    for (my $i = 1; $i < scalar(@ol); $i++) {
		$ol[$i] .= ',ND';
	    }
	    return(@ol);
    }
};

# Common pattern for the HINT_NOPx pseudo-instructions
$macros{'hint_nops'} = {
    'func' =>
	sub {
	    my($mac, $args, $rawargs) = @_;
	    my @ol;

	    for (my $i = 0; $i < 64; $i++) {
		push(@ol,
		     sprintf("\$wdq HINT_NOP%d\trm#\t[m: o# 0f %02x /%d]\tP6,UNDOC,ND",
			     $i, 0x18+($i >> 3), $i & 7));
	    }
	    return @ol;
    }
};

#
# Macro helper functions for common constructs
#

# Parse arguments handling variable setting
sub parse_args($@) {
    my $uvars = shift(@_);
    my %initvars = defined($uvars) ? %$uvars : ();
    my @oa;
    my $n = 0;			# Argument counter

    foreach my $ops (@_) {
	my %vars = %initvars;
	$vars{'n'}  = $n;
	$vars{'nd'} = 0;
	my @oaa;
	my $is_op;
	foreach my $op ($ops =~ /(?:[^\,\[\]\"]+|\[.*?\]|\".*?\")+/g) {
	    $op =~ s/\"//g;

	    $vars{'nd'} = 'nd' if ($op =~ s/^\@//);
	    if ($op =~ /^(\w+)\=(.*)$/) {
		$vars{$1} = $2;
		next;
	    } elsif ($op =~ /^([\!\+\-])(\w+)$/) {
		# The commas around KILL guarantees that it is a separate token
		$vars{$2} =
		    ($1 eq '+') ? $2 :
		    ($1 eq '!') ? ',KILL,' :
		    '';
		next;
	    } elsif ($op =~ /^\-?$/) {
		# Null (placeholder) operand
		$is_op = 1;
		next;
	    }

	    $vars{'op'} = $op;
	    push(@oaa, {%vars});
	    $vars{'nd'} = 'nd';
	    $is_op = 1;
	}
	if ($is_op) {
	    push(@oa, [@oaa]);
	    $n++;
	} else {
	    # Global variable setting
	    %initvars = %vars;
	}
    }

    return @oa;
}

# "8-fold" or similar sequential instruction patterns
sub def_eightfold($$$) {
    my($var, $arg, $mac) = @_;

    my $shift = $arg->{'shift'};
    $shift = 3 unless (defined($shift));

    if ($var =~ /^[0-9a-f]{1,2}$/) {
	return sprintf('%02x', hex($var) + ($arg->{'n'} << $shift));
    } else {
	return $var;
    }
}

#
# Substitute variables in a pattern
#
sub substitute($$;$) {
    my($pat, $vars, $defs) = @_;
    my $o = '';
    my $def;
    my @defargs;

    if (defined($defs)) {
	@defargs = @$defs;
	$def = shift(@defargs);
    }

    while ($pat =~ /^(.*?)\$(?:(\w+\b|\$+)|\{(\w+|\$+)\})(.*)$/s) {
	$o .= $1;
	$pat = $4;
	my $vn = $2.$3;
	my $vv;
	if ($vn =~ /^\$/) {
	    $vv = $vn;		# Reduce by one $
	} else {
	    $vv = $vars->{$vn};
	    if (!defined($vv)) {
		if (defined($def)) {
		    $vv = $def->($vn, @defargs);
		}
		$vv = $vn unless(defined($vv));
	    }
	}
	$vv =~ s/\s+$// if ($pat =~ /^\s/);
	$o .= $vv;
    }
    $o .= $pat;

    return $o;
}

#
# Build output by substituting the variables for each argument,
#
sub subst_list($$;$$) {
    my($pat, $args, $def, $mac) = @_;
    my @o = ();

    foreach my $a0 (@$args) {
	foreach my $arg (@$a0) {
	    push(@o, substitute($pat, $arg, [$def, $arg, $mac]));
	}
    }

    return @o;
}

#
# Actually invoke a macro
#
sub process_macro(@) {
    $macro = shift(@_);
    my $mac = $macros{$macro};

    if (!defined($mac)) {
	die "$0:$infile:$line: no macro named \$$macro\n";
    }

    my @args = parse_args($mac->{'vars'}, @_);
    my $func = $mac->{'func'};
    my @o;
    if (defined($func)) {
	@o = $func->($mac, \@args, \@_);
    } else {
	@o = subst_list($mac->{'txt'}, \@args, $mac->{'def'}, $mac);
    }
    return map { split(/\n/, $_) } @o;
}

#
# Main program
#
($infile, $outfile) = @ARGV;
$line = 0;

## XXX: fix special case: XCHG
## XXX: check: CMPSS, CMPSD
## XXX: check VEX encoded instructions that do not write

# Instructions which (possibly) change the flags without annotations
# The FL or !FL flags will override this
my $flaggy = '^(aa[adms]|ad[dc]|ad[co]x|aes\w*kl|and|andn|arpl|bextr|bl[sc]ic?|bl[sc]msk|bl[sc]r|\
bs[rf]|bt|bt[crs]|bzhi|clac|clc|cld|cli|clrssbsy|cmc|cmp|cmpxchg.*|da[as]|dec|div|\
encodekey.*|enqcmd.*|fu?comip?|idiv|imul|inc|iret.*|kortest.*|ktest.*|lar|loadiwkey|\
lsl|[lt]zcnt|mul|neg|or|pconfig|popcnt|popf.*|r[co][lr]|rdrand|rdseed|sahf|s[ah][lr]|\
sbb|scas.*|sh[lr]d|stac|stc|std|sti|sub|test|testui|tpause|v?u?comis[sdh]|uiret|\
umwait|ver[rw]|vtestp[ps]|xadd|xor|xtest|getsec|rsm|sbb|cmps[bwdq]|hint_.*)$';

# Instructions which don't write their leftmost operand are inherently not {zu}
my $nozero = '^(jmp|call|bt|test|cmp|ud[012].*|ptwrite|tpause|u?monitor.*|u?mwait.*|incssp.*|\
enqcmds?|senduipi|hint_.*|jmpe|nop|inv.*|push2?p?|vmwrite|clzero|clflush|clwb|lkgs)$';

sub add_flag($@) {
    my $flags = shift(@_);

    foreach my $fl (@_) {
	$flags->{$fl}++ unless ($fl =~ /^\s*$/);
    }
}

sub has_flag($@) {
    my $flags = shift(@_);
    foreach my $fl (@_) {
	return $flags->{$fl} if ($flags->{$fl});
    }
    return undef;
}

sub adjust_fl_zu(@) {
    my($opcode, $operands, $encoding, $flags) = @_;

    # Flag-changing instructions
    if ($encoding =~ /\bnf\b/) {
	add_flag($flags, 'NF');
    }

    if (!has_flag($flags, '!FL', 'NF', 'NF!')) {
	add_flag($flags, 'FL') if ($opcode =~ /$flaggy/io);
    }

    ## XXX: fix special case: XCHG
    ## XXX: check: CMPSS, CMPSD
    ## XXX: check VEX encoded instructions that do not write

    # Zero-upper. This can also be used to select the AVX forms
    # to clear the upper part of a vector register.
    if (!$flags->{'!ZU'} &&
	(($encoding =~ /\be?vex\b/ && $operands =~ /^(xyz)mm(reg|rm)/) ||
	 $operands =~ /^(reg_[re]([abcd]x|[sb]p|[sd]i))\b/) &&
	$opcode !~ /$nozero/io) {
	add_flag($flags, 'ZU');
    }

    return $flags;
}

sub adjust_instruction_flags(@) {
    my @i = @_;

    $i[3] = adjust_fl_zu(@i);

    return undef unless (defined($i[3]));

    if ($i[0] =~ /\bKILL\b/ || $i[1] =~ /\bKILL\b/ ||
	$i[2] =~ /\bKILL\b/ || has_flag($i[3], 'KILL')) {
	return undef;
    }

    if ($i[2] =~ /\b(a16|rex\.l)\b/) {
	add_flag($i[3], 'NOLONG');
    } elsif ($i[2] =~ /\b(o64(nw)?\b|rex2?|a64\b)/) {
	add_flag($i[3], 'LONG');
    }
    if (has_flag($i[3], 'NOLONG') && has_flag($i[3], 'LONG')) {
	# This is obviously not very useful...
	return undef;
    }

    if (has_flag($i[3], 'LONG')) {
	add_flag($i[3], 'X86_64');
    }

    return $i[3];
}

sub process_insn($$) {
    my($out, $l) = @_;

    if ($l !~ /^\s*([^\s\;]+)\s+([^\s\;]+)\s+([^\s\;]+|\[[^\;]*?\])\s+([^\s\;]+)\s*(\;.*?)?\s*$/) {
	print $out $l, "\n";
	return;
    }

    print $out $5, "\n" unless ($5 eq ''); # Comment

    my $opcode   = $1;
    my $operands = $2;
    my $encoding = $3;
    my $flagstr  = $4;

    my $nopr = ($operands =~ /^(void|ignore)$/) ? 0 : scalar(split(/[\,\:]/, $operands));

    # Modify the instruction flags
    my $flags = {split_flags($flagstr)};
    set_implied_flags($flags, $nopr);

    $flags = adjust_instruction_flags($opcode, $operands, $encoding, $flags);
    return unless (defined($flags));
    $flagstr = merge_flags($flags, 1);

    # Tidy up the encoding for readability
    $encoding =~ s/\s+/ /g;
    if ($encoding =~ /^\s*\[\s*(.*?\:)?\s*([^\:]*?)\s*\]\s*$/) {
	$encoding = sprintf('[%-10s%-34s]', $1, $2);
    }

    print $out sprintf("%-23s %-39s %-47s %s\n", $opcode, $operands, $encoding, $flagstr);
}

open(my $in, '<', $infile) or die "$0:$infile: $!\n";
open(my $out, '>', $outfile) or die "$0:$outfile: $!\n";


while (defined(my $l = <$in>)) {
    $line++;
    chomp $l;
    my @insi = ($l);

    while (defined(my $li = shift(@insi))) {
	if ($li =~ /^\s*\$(\w+[^\;]*?)\s*(\;.*)?$/) {
	    push(@insi, "$2\n") unless ($2 eq ''); # Retain comment
	    my @args = ($1 =~ /(?:\[[^\]]*\]|\"[^\"]*\"|[^\[\]\"\s])+/g);
	    unshift(@insi, process_macro(@args));
	} else {
	    process_insn($out, $li);
	}
    }
}

close($in);
close($out);
