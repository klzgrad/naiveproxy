#!/usr/bin/perl
## --------------------------------------------------------------------------
##   
##   Copyright 1996-2014 The NASM Authors - All Rights Reserved
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
# Generate a perfect hash for token parsing
#
# Usage: tokenhash.pl insns.dat regs.dat tokens.dat
#

require 'phash.ph';

my($output, $insns_dat, $regs_dat, $tokens_dat) = @ARGV;

%tokens = ();
@tokendata = ();

#
# List of condition codes
#
@conditions = ('a', 'ae', 'b', 'be', 'c', 'e', 'g', 'ge', 'l', 'le',
	       'na', 'nae', 'nb', 'nbe', 'nc', 'ne', 'ng', 'nge', 'nl',
	       'nle', 'no', 'np', 'ns', 'nz', 'o', 'p', 'pe', 'po', 's', 'z');

#
# Read insns.dat
#
open(ID, '<', $insns_dat) or die "$0: cannot open $insns_dat: $!\n";
while (defined($line = <ID>)) {
    if ($line =~ /^([A-Z0-9_]+)(|cc)\s/) {
	$insn = $1.$2;
	($token = $1) =~ tr/A-Z/a-z/;

	if ($2 eq '') {
	    # Single instruction token
	    if (!defined($tokens{$token})) {
		$tokens{$token} = scalar @tokendata;
		push(@tokendata, "\"${token}\", TOKEN_INSN, C_none, 0, I_${insn}");
	    }
	} else {
	    # Conditional instruction
	    foreach $cc (@conditions) {
		if (!defined($tokens{$token.$cc})) {
		    $tokens{$token.$cc} = scalar @tokendata;
		    push(@tokendata, "\"${token}${cc}\", TOKEN_INSN, C_\U$cc\E, 0, I_${insn}");
		}
	    }
	}
    }
}
close(ID);

#
# Read regs.dat
#
open(RD, '<', $regs_dat) or die "$0: cannot open $regs_dat: $!\n";
while (defined($line = <RD>)) {
    if ($line =~ /^([a-z0-9_-]+)\s*\S+\s*\S+\s*[0-9]+\s*(\S*)/) {
	$reg = $1;
	$reg_flag = $2;

	if ($reg =~ /^(.*[^0-9])([0-9]+)\-([0-9]+)(|[^0-9].*)$/) {
	    $nregs = $3-$2+1;
	    $reg = $1.$2.$4;
	    $reg_nr = $2;
	    $reg_prefix = $1;
	    $reg_suffix = $4;
	} else {
	    $nregs = 1;
	    undef $reg_prefix, $reg_suffix;
	}

	while ($nregs--) {
	    if (defined($tokens{$reg})) {
		die "Duplicate definition: $reg\n";
	    }
	    $tokens{$reg} = scalar @tokendata;
	    if ($reg_flag eq '') {
	        push(@tokendata, "\"${reg}\", TOKEN_REG, 0, 0, R_\U${reg}\E");
	    } else {
	        push(@tokendata, "\"${reg}\", TOKEN_REG, 0, ${reg_flag}, R_\U${reg}\E");
	    }

	    if (defined($reg_prefix)) {
		$reg_nr++;
		$reg = sprintf("%s%u%s", $reg_prefix, $reg_nr, $reg_suffix);
	    } else {
		# Not a dashed sequence
		die if ($nregs);
	    }
	}
    }
}
close(RD);

#
# Read tokens.dat
#
open(TD, '<', $tokens_dat) or die "$0: cannot open $tokens_dat: $!\n";
while (defined($line = <TD>)) {
    if ($line =~ /^\%\s+(.*)$/) {
	$pattern = $1;
    } elsif ($line =~ /^([a-z0-9_-]+)/) {
	$token = $1;

	if (defined($tokens{$token})) {
	    die "Duplicate definition: $token\n";
	}
	$tokens{$token} = scalar @tokendata;

	$data = $pattern;
	if ($data =~ /^(.*)\{(.*)\}(.*)$/) {
	    my $head = $1, $tail = $3;
	    my $px = $2;

	    $px =~ s/\*/(.*)/g;
	    if ($token =~ /$px/i) {
		$data = $head."\U$1".$tail;
	    } else {
		die "$0: token $token doesn't match $px\n";
	    }
	}

	$data =~ s/\*/\U$token/g;

	push(@tokendata, "\"$token\", $data");
    }
}
close(TD);

if ($output eq 'h') {
    #
    # tokens.h
    #

    $max_len = 0;
    foreach $token (keys(%tokens)) {
	if (length($token) > $max_len) {
	    $max_len = length($token);
	}
    }

    print "/*\n";
    print " * This file is generated from insns.dat, regs.dat and token.dat\n";
    print " * by tokhash.pl; do not edit.\n";
    print " */\n";
    print "\n";

    print "#ifndef NASM_TOKENS_H\n";
    print "#define NASM_TOKENS_H\n";
    print "\n";
    print "#define MAX_KEYWORD $max_len /* length of longest keyword */\n";
    print "\n";
    print "#endif /* NASM_TOKENS_H */\n";
} elsif ($output eq 'c') {
    #
    # tokhash.c
    #

    @hashinfo = gen_perfect_hash(\%tokens);
    if (!@hashinfo) {
	die "$0: no hash found\n";
    }

    # Paranoia...
    verify_hash_table(\%tokens, \@hashinfo);

    ($n, $sv, $g) = @hashinfo;
    $sv2 = $sv+2;

    die if ($n & ($n-1));

    print "/*\n";
    print " * This file is generated from insns.dat, regs.dat and token.dat\n";
    print " * by tokhash.pl; do not edit.\n";
    print " */\n";
    print "\n";

    print "#include \"compiler.h\"\n";
    print "#include <string.h>\n";
    print "#include \"nasm.h\"\n";
    print "#include \"hashtbl.h\"\n";
    print "#include \"insns.h\"\n";
    print "#include \"stdscan.h\"\n";
    print "\n";

    # These somewhat odd sizes and ordering thereof are due to the
    # relative ranges of the types; this makes it fit in 16 bytes on
    # 64-bit machines and 12 bytes on 32-bit machines.
    print "struct tokendata {\n";
    print "    const char *string;\n";
    print "    int16_t tokentype;\n";
    print "    int8_t aux;\n";
    print "    int8_t tokflag;\n";
    print "    int32_t num;\n";
    print "};\n";
    print "\n";

    print "int nasm_token_hash(const char *token, struct tokenval *tv)\n";
    print "{\n";

    # Put a large value in unused slots.  This makes it extremely unlikely
    # that any combination that involves unused slot will pass the range test.
    # This speeds up rejection of unrecognized tokens, i.e. identifiers.
    print "#define UNUSED (65535/3)\n";

    print "    static const int16_t hash1[$n] = {\n";
    for ($i = 0; $i < $n; $i++) {
	my $h = ${$g}[$i*2+0];
	print "        ", defined($h) ? $h : 'UNUSED', ",\n";
    }
    print "    };\n";

    print "    static const int16_t hash2[$n] = {\n";
    for ($i = 0; $i < $n; $i++) {
	my $h = ${$g}[$i*2+1];
	print "        ", defined($h) ? $h : 'UNUSED', ",\n";
    }
    print "    };\n";

    printf "    static const struct tokendata tokendata[%d] = {\n", scalar(@tokendata);
    foreach $d (@tokendata) {
	print "        { ", $d, " },\n";
    }
    print  "    };\n";

    print  "    uint32_t k1, k2;\n";
    print  "    uint64_t crc;\n";
    # For correct overflow behavior, "ix" should be unsigned of the same
    # width as the hash arrays.
    print  "    uint16_t ix;\n";
    print  "    const struct tokendata *data;\n";
    print  "\n";
    printf "    tv->t_flag = 0;\n";
    printf "    crc = crc64(UINT64_C(0x%08x%08x), token);\n",
	$$sv[0], $$sv[1];
    print  "    k1 = (uint32_t)crc;\n";
    print  "    k2 = (uint32_t)(crc >> 32);\n";
    print  "\n";
    printf "    ix = hash1[k1 & 0x%x] + hash2[k2 & 0x%x];\n", $n-1, $n-1;
    printf "    if (ix >= %d)\n", scalar(@tokendata);
    print  "        return tv->t_type = TOKEN_ID;\n";
    print  "\n";
    print  "    data = &tokendata[ix];\n";

    print  "    if (strcmp(data->string, token))\n";
    print  "        return tv->t_type = TOKEN_ID;\n";
    print  "\n";
    print  "    tv->t_integer = data->num;\n";
    print  "    tv->t_inttwo  = data->aux;\n";
    print  "    tv->t_flag    = data->tokflag;\n";
    print  "    return tv->t_type = data->tokentype;\n";
    print  "}\n";
}
