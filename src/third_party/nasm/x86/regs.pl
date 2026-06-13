#!/usr/bin/perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright 1996-2025 The NASM Authors - All Rights Reserved

#
# Read regs.dat and output regs.h and regs.c (included in names.c)
#

use integer;

# For simplicity make all the disassembly tables the same size
# This must be power of 2!
my $distablesz = 32;

die if ($distablesz & ($distablesz-1)); # Not a power of 2?

$nline = 0;

sub toint($) {
    my($v) = @_;

    return ($v =~ /^0/) ? oct $v : $v+0;
}

sub process_line($) {
    my($line) = @_;

    if ( $line !~ /^\s*(\S+)\s*(\S+)\s*(\S+)\s*([0-9]+)\s*(?:\&\s*(0x[0-9a-f]+|[0-9]+))?/i ) {
	die "regs.dat:$nline: invalid input\n";
    }
    $reg      = $1;
    $aclass   = $2;
    $dclasses = $3;
    $x86regno = toint($4);
    $regmask  = toint($5);

    if ($reg =~ /^(.*[^0-9])([0-9]+)\-([0-9]+)(|[^0-9].*)$/) {
	$nregs = $3-$2+1;
	$reg = $1.$2.$4;
	$reg_nr = $2;
	$reg_prefix = $1;
	$reg_suffix = $4;
    } else {
	$nregs = 1;
	undef $reg_prefix;
	undef $reg_suffix;
    }

    foreach $dclass (split(/,/, $dclasses)) {
	if ( !defined($disclass{$dclass}) ) {
	    $disclass{$dclass} = [];
	}
	if ($regmask) {
	    if ($regmasks{$dclass} && $regmasks{$dclass} != $regmask) {
		die "regs.dat:$nline: inconsistent regmasks for disassembly class $dclass\n";
	    }
	    $regmasks{$dclass} = $regmask;
	}
    }

    while ($nregs--) {
	$regs{$reg} = $aclass;
	$regvals{$reg} = $x86regno;

	if ($x86regno >= $distablesz) {
	    die "regs.dat:$nline: register number $x86regno too large, increase distablesz in regs.pl\n";
	}

	foreach $dclass (split(/,/, $dclasses)) {
	    $disclass{$dclass}->[$x86regno] = $reg;
	}

	# Compute the next register, if any
	if (defined($reg_prefix)) {
	    $x86regno++;
	    $reg_nr++;
	    $reg = sprintf("%s%u%s", $reg_prefix, $reg_nr, $reg_suffix);
	} else {
	    # Not a dashed sequence
	    die if ($nregs);
	}
    }
}

($fmt, $file) = @ARGV;

%regs = ();
%regvals = ();
%disclass = ();
%regmasks = ();
open(REGS, '<', $file) or die "$0: Cannot open $file\n";
while ( defined($line = <REGS>) ) {
    $nline++;

    chomp $line;
    $line =~ s/\s*(\#.*|)$//;

    next if ( $line eq '' );

    process_line($line);
}
close(REGS);

if ( $fmt eq 'h' ) {
    # Output regs.h
    print "/* automatically generated from $file - do not edit */\n\n";
    print "#ifndef NASM_REGS_H\n";
    print "#define NASM_REGS_H\n\n";

    $expr_regs = 1;
    printf "#define EXPR_REG_START %d\n\n", $expr_regs;
    print "enum reg_enum {\n";
    # Unfortunately the code uses both 0 and -1 as "no register" in
    # different places...
    print "    R_zero = 0,\n";
    print "    R_none = -1,\n";
    $attach = ' = EXPR_REG_START'; # EXPR_REG_START == 1
    foreach $reg ( sort(keys(%regs)) ) {
	print "    R_\U${reg}\E${attach},\n";
	$attach = '';
	$expr_regs++;
    }
    print "    REG_ENUM_LIMIT\n";
    print "};\n\n";
    printf "#define EXPR_REG_END %d\n\n", $expr_regs-1;
    foreach $reg ( sort(keys(%regs)) ) {
	printf "#define %-15s %2d\n", "REG_NUM_\U${reg}", $regvals{$reg};
    }
    print "\n\n#endif /* NASM_REGS_H */\n";
} elsif ( $fmt eq 'c' ) {
    # Output regs.c
    print "/* automatically generated from $file - do not edit */\n\n";
    print "#include \"tables.h\"\n\n";
    print "const char * const nasm_reg_names[] = "; $ch = '{';
    # This one has no dummy entry for 0
    foreach $reg ( sort(keys(%regs)) ) {
	print "$ch\n    \"${reg}\"";
	$ch = ',';
    }
    print "\n};\n";
} elsif ( $fmt eq 'fc' ) {
    # Output regflags.c
    print "/* automatically generated from $file - do not edit */\n\n";
    print "#include \"tables.h\"\n";
    print "#include \"nasm.h\"\n\n";
    print "const opflags_t nasm_reg_flags[] = {\n";
    printf "    0,\n";		# Dummy entry for 0
    foreach $reg ( sort(keys(%regs)) ) {
	# Print the class of the register
	printf "    /* %-5s */    RN_FLAGS(%2d) | %s,\n",
	    $reg, $regvals{$reg}, $regs{$reg};
    }
    print "};\n";
} elsif ( $fmt eq 'vc' ) {
    # Output regvals.c
    print "/* automatically generated from $file - do not edit */\n\n";
    print "#include \"tables.h\"\n\n";
    print "const int nasm_regvals[] = {\n";
    print "    -1,\n";		# Dummy entry for 0
    foreach $reg ( sort(keys(%regs)) ) {
	# Print the x86 value of the register
	printf "    %2d,  /* %-5s */\n", $regvals{$reg}, $reg;
    }
    print "};\n";
} elsif ( $fmt eq 'dc' ) {
    # Output regdis.c
    print "/* automatically generated from $file - do not edit */\n\n";
    print "#include \"regdis.h\"\n\n";
    foreach $class ( sort(keys(%disclass)) ) {
	die if (scalar @{$disclass{$class}} > $distablesz);
	$regmask = $regmasks{$class} || ($distablesz - 1);
	printf "const enum reg_enum nasm_rd_%s[DISREGTBLSZ] = {",
	    $class;
	for ($i = 0; $i < $distablesz; $i++) {
	    my $r = $disclass{$class}->[$i & $regmask];
	    print "\n   " if (!($i & 7));
	    print ' ', defined($r) ? "R_\U$r" : '0';
	    print ',' unless ($i == $distablesz - 1);
	}
	print "\n};\n";
    }
} elsif ( $fmt eq 'dh' ) {
    # Output regdis.h
    print "/* automatically generated from $file - do not edit */\n\n";
    print "#ifndef NASM_REGDIS_H\n";
    print "#define NASM_REGDIS_H\n\n";
    print "#include \"regs.h\"\n\n";
    printf "#define DISREGTBLSZ %d\n\n", $distablesz;
    foreach $class ( sort(keys(%disclass)) ) {
	printf "extern const enum reg_enum nasm_rd_%s[DISREGTBLSZ];\n",
	    $class;
    }
    print "\n#endif /* NASM_REGDIS_H */\n";
} else {
    die "$0: Unknown output format\n";
}
