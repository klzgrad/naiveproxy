#!/usr/bin/perl
## --------------------------------------------------------------------------
##
##   Copyright 1996-2017 The NASM Authors - All Rights Reserved
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
# Generate a perfect hash for general case-insensitive string-to-enum
# lookup.  This generates an enum and the corresponding hash, but
# relies on a common function to parse the hash.
#
# Usage:
#      perfhash.pl h foohash.dat foohash.h (to generate C header)
#      perfhash.pl c foohash.dat foohash.c (to generate C source)
#

use strict;

require 'phash.ph';

sub basename($) {
    my($s) = @_;
    $s =~ s/^.*[^-[:alnum:]_\.]//;	# Remove path component as best we can
    return $s;
}

sub intval($) {
    my($s) = @_;

    if ($s =~ /^0/) {
	return oct($s);		# Handles octal or hexadecimal
    } elsif ($s =~ /^\-(0.*)$/) {
	return -oct($1);
    } else {
	return $s + 0;		# Forcibly convert to number
    }
}

my($output, $infile, $outfile) = @ARGV;
my $me = basename($0);

# The following special things are allowed in the input file:
# #<space> or ; begins a comment
# #include filename
# #name str
#    The name of the hash
# #prefix str
#    Defines the prefix before enum
# #guard str
#    Defines the header guard string
# #special str [= value]
#    Generate an enum value without a corresponding string; not capitalized.
# #header str
#    Indicates the name of the .h file to include from the .c file
# #errval str
#    Define the value to be returned if a string is not found
#    (defaults to -1).  This can be any constant C expression,
#    including one of the enum values.
#
# Regular lines are just str [= value]
#
# Enumeration is generated in the order listed in the file, just as in C
# specifying a value causes the values to increase by 1 from that point on
# unless specified.

my $name;
my $prefix;
my $guard;
my $hfile;

my %strings = ();
my %specials = ();
my $next_value = 0;
my $errval = '-1';

my @incstack =  ();
my @filenames = ($infile);
my @linenums =  (0);
my $dd = undef;
my $err = 0;

while (scalar(@filenames)) {
    if (!defined($dd)) {
	open($dd, '<', $filenames[-1])
	    or die "$0: cannot open: $filenames[-1]: $!\n";
    }

    my $line = <$dd>;
    if (!defined($line)) {
	close($dd);
	$dd = pop @incstack;
	pop @filenames;
	pop @linenums;
	next;
    }

    $linenums[-1]++;

    chomp $line;
    $line =~ s/\s*(|\;.*|\#\s.*|\#)$//; # Remove comments and trailing space
    $line =~ s/^\s+//;			# Remove leading space
    if ($line eq '') {
	# Do nothing
    } elsif ($line =~ /^\#name\s+([[:alnum:]_]+)$/) {
	$name = $1;
    } elsif ($line =~ /^\#prefix\s+([[:alnum:]_]+)$/) {
	$prefix = $1;
    } elsif ($line =~ /^\#guard\s+([[:alnum:]_]+)$/) {
	$guard = $1;
    } elsif ($line =~ /^\#errval\s+(\S.*)$/) {
	$errval = $1;
    } elsif ($line =~ /^\#header\s+(\"(.+)\"|\S+)$/) {
	$hfile = ($2 ne '') ? $2 : $1;
    } elsif ($line =~ /^\#include\s+(\"(.+)\"|\S+)$/) {
	push @incstack, $dd;
	push @filenames, (($2 ne '') ? $2 : $1);
	push @linenums, 0;
	undef $dd;		# Open a new file
    } elsif ($line =~ /^(|\#special\s+)(\S+)\s*(|=\s*(\-?(0[Xx][[:xdigit:]]+|0[0-7]*|[0-9]+)))$/) {
	$next_value = intval($4) if ($4 ne '');
	if ($1 eq '') {
	    $strings{$2} = $next_value++;
	} else {
	    $specials{$2} = $next_value++;
	}
    } else {
	printf STDERR "%s:%d:%s syntax error: \"%s\"\n",
	    $filenames[-1], $linenums[-1],
	    (scalar(@incstack) == 1) ? '' : "(from $infile)", $line;
	$err++;
    }
}

exit 1 if ($err);

# Default name, prefix, and header guard name
if (!defined($name)) {
    $name = basename($infile);
    $name =~ s/(\..*)$//;		# Strip extension, if any
}
if (!defined($prefix)) {
    $prefix = "\U${name}\E_";
}
if (!defined($hfile)) {
    $hfile = $outfile;
    $hfile =~ s/\.c$/\.h/;
}
if (!defined($guard)) {
    $guard = basename($hfile);
    $guard =~ s/[^[:alnum:]_]/_/g;
    $guard =~ s/__+/_/g;
    $guard =  "\U$guard";
}

# Verify input.  We can't have more than one constant with the same
# enumeration value, nor the same enumeration string.
if (scalar(keys(%strings)) == 0) {
    die "$0: $infile: no strings to hash!\n";
}

my %enums;
my %enumvals;
my %stringbyval;
my $max_enum;
my $tbllen = 0;
my $tbloffs;
foreach my $s (keys(%strings)) {
    my $es = "${prefix}\U${s}";
    $es =~ s/[^[:alnum:]_]/_/g;
    $es =~ s/__+/_/g;
    my $v = $strings{$s};
    $stringbyval{$v} = $s;
    if (defined($enums{$es})) {
	printf STDERR "%s: string \"%s\" duplicates existing enum %s\n",
	    $infile, $s, $es;
	$err++;
    } else {
	$enums{$es} = $v;
    }
    if (defined($enumvals{$v})) {
	printf STDERR "%s: string \"%s\" duplicates existing enum constant %d\n", $v;
	$err++;
    } else {
	$enumvals{$v} = $es;
    }
    $max_enum = $v if ($v > $max_enum || !defined($max_enum));
    $tbloffs = $v if ($v < $tbloffs || !defined($tbloffs));
    $tbllen = $v+1 if ($v >= $tbllen || !defined($tbllen));
}
foreach my $s (keys(%specials)) {
    my $es = $prefix . $s;	# No string mangling here
    my $v = $specials{$s};
    if (defined($enums{$es})) {
	printf STDERR "%s: special \"%s\" duplicates existing enum %s\n",
	    $infile, $s, $es;
	$err++;
    } else {
	$enums{$es} = $v;
    }
    if (defined ($enumvals{$v})) {
	printf STDERR "%s: special \"%s\" duplicates existing enum constant %d\n", $v;
	$err++;
    } else {
	$enumvals{$v} = $es;
    }
    $max_enum = $v if ($v > $max_enum || !defined($max_enum));
}

$tbllen -= $tbloffs;
if ($tbllen > 65536) {
    printf STDERR "%s: span of enumeration values too large\n";
    $err++;
}

exit 1 if ($err);

open(F, '>', $outfile)
    or die "$0: cannot create: ${outfile}: $!\n";

if ($output eq 'h') {
    print F "/*\n";
    print F " * This file is generated from $infile\n";
    print F " * by $me; do not edit.\n";
    print F " */\n";
    print F "\n";

    print F "#ifndef $guard\n";
    print F "#define $guard 1\n\n";
    print F "#include \"perfhash.h\"\n\n";

    my $c = '{';
    $next_value = 0;
    print F "enum ${name} ";
    foreach my $v (sort { $a <=> $b } keys(%enumvals)) {
	my $s = $enumvals{$v};
	print F "$c\n    $s";
	print F " = $v" if ($v != $next_value);
	$next_value = $v + 1;
	$c = ',';
    }
    print F "\n};\n\n";
    print F "extern const struct perfect_hash ${name}_hash;\n";
    printf F "extern const char * const %s_tbl[%d];\n", $name, $tbllen;

    print F "\nstatic inline enum ${name} ${name}_find(const char *str)\n";
    print F "{\n";
    print F "    return perfhash_find(&${name}_hash, str);\n";
    print F "}\n";

    print F "\nstatic inline const char * ${name}_name(enum ${name} x)\n";
    print F "{\n";
    printf F  "    size_t ix = (size_t)x - (%d);\n", $tbloffs;
    printf F "    if (ix >= %d)\n", $tbllen;
    print F  "        return NULL;\n";
    print F  "    return ${name}_tbl[ix];\n";
    print F  "}\n";

    print F "\nstatic inline const char * ${name}_dname(enum ${name} x)\n";
    print F "{\n";
    print F "    const char *y = ${name}_name(x);\n";
    print F "    return y ? y : invalid_enum_str(x);\n";
    print F "}\n";

    print F "\n#endif /* $guard */\n";
} elsif ($output eq 'c') {
    # The strings we hash must all be lower case, even if the string
    # table doesn't contain them that way.

    my %lcstrings;
    foreach my $s (keys(%strings)) {
	my $ls = "\L$s";
	if (defined($lcstrings{$ls})) {
	    printf STDERR "%s: strings \"%s\" and \"%s\" differ only in case\n",
		$infile, $s, $strings{$lcstrings{$s}};
	} else {
	    $lcstrings{$ls} = $strings{$s} - $tbloffs;
	}
    }

    my @hashinfo = gen_perfect_hash(\%lcstrings);
    if (!@hashinfo) {
	die "$0: no hash found\n";
    }

    # Paranoia...
    verify_hash_table(\%lcstrings, \@hashinfo);

    my ($n, $sv, $g) = @hashinfo;

    die if ($n & ($n-1));

    print F "/*\n";
    print F " * This file is generated from $infile\n";
    print F " * by $me; do not edit.\n";
    print F " */\n";
    print F "\n";

    print F "#include \"$hfile\"\n\n";

    printf F "const char * const %s_tbl[%d] = ", $name, $tbllen;
    my $c = '{';
    for (my $i = $tbloffs; $i < $tbloffs+$tbllen; $i++) {
	printf F "%s\n    %s", $c,
	    defined($stringbyval{$i}) ? '"'.$stringbyval{$i}.'"' : 'NULL';
	$c = ',';
    }
    print F "\n};\n\n";

    print F "#define UNUSED (65536/3)\n\n";

    printf F "static const int16_t %s_hashvals[%d] = ", $name, $n*2;
    $c = '{';
    for (my $i = 0; $i < $n; $i++) {
	my $h = ${$g}[$i*2+0];
	print F "$c\n    ", defined($h) ? $h : 'UNUSED';
	$c = ',';
    }
    for (my $i = 0; $i < $n; $i++) {
	my $h = ${$g}[$i*2+1];
	print F "$c\n    ", defined($h) ? $h : 'UNUSED';
	$c = ',';
    }
    print F "\n};\n\n";

    print F "const struct perfect_hash ${name}_hash = {\n";
    printf F "    UINT64_C(0x%08x%08x),\n", $$sv[0], $$sv[1]; # crcinit
    printf F "    UINT32_C(0x%x),\n", $n-1;		      # hashmask
    printf F "    UINT32_C(%u),\n", $tbllen;		      # tbllen
    printf F "    %d,\n", $tbloffs;			      # tbloffs
    printf F "    (%s),\n", $errval;			      # errval
    printf F "    ${name}_hashvals,\n";			      # hashvals
    printf F "    ${name}_tbl\n";			      # strings
    print F "};\n";
}
