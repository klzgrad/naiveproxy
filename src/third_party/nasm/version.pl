#!/usr/bin/perl
## --------------------------------------------------------------------------
##   
##   Copyright 1996-2016 The NASM Authors - All Rights Reserved
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
# version.pl
#
# Parse the NASM version file and produce appropriate macros
#
# The NASM version number is assumed to consist of:
#
# <major>.<minor>[.<subminor>][pl<patchlevel>]]<tail>
#
# ... where <tail> is not necessarily numeric, but if it is of the form
# -<digits> it is assumed to be a snapshot release.
#
# This defines the following macros:
#
# version.h:
# NASM_MAJOR_VER
# NASM_MINOR_VER
# NASM_SUBMINOR_VER	-- this is zero if no subminor
# NASM_PATCHLEVEL_VER	-- this is zero is no patchlevel
# NASM_SNAPSHOT		-- if snapshot
# NASM_VERSION_ID       -- version number encoded
# NASM_VER		-- whole version number as a string
#
# version.mac:
# __NASM_MAJOR__
# __NASM_MINOR__
# __NASM_SUBMINOR__
# __NASM_PATCHLEVEL__
# __NASM_SNAPSHOT__
# __NASM_VERSION_ID__
# __NASM_VER__
#

($what) = @ARGV;

$line = <STDIN>;
chomp $line;

undef $man, $min, $smin, $plvl, $tail;
$is_rc = 0;

if ( $line =~ /^([0-9]+)\.([0-9]+)(.*)$/ ) {
    $maj  = $1;
    $min  = $2;
    $tail = $3;
    if ( $tail =~ /^\.([0-9]+)(.*)$/ ) {
	$smin = $1;
	$tail = $2;
    }
    if ( $tail =~ /^(pl|\.)([0-9]+)(.*)$/ ) {
	$plvl = $2;
	$tail = $3;
    } elsif ( $tail =~ /^rc([0-9]+)(.*)$/ ) {
	$is_rc = 1;
	$plvl = $1;
	$tail = $2;
    }
} else {
    die "$0: Invalid input format\n";
}

if ($tail =~ /^\-([0-9]+)$/) {
    $snapshot = $1;
} else {
    undef $snapshot;
}

$nmaj = $maj+0;   $nmin = $min+0;
$nsmin = $smin+0; $nplvl = $plvl+0;

if ($is_rc) {
    $nplvl += 90;
    if ($nsmin > 0) {
	$nsmin--;
    } else {
	$nsmin = 99;
	if ($nmin > 0) {
	    $nmin--;
	} else {
	    $nmin = 99;
	    $nmaj--;
	}
    }
}

$nasm_id = ($nmaj << 24)+($nmin << 16)+($nsmin << 8)+$nplvl;

$mangled_ver = sprintf("%d.%02d", $nmaj, $nmin);
if ($nsmin || $nplvl || defined($snapshot)) {
    $mangled_ver .= sprintf(".%02d", $nsmin);
    if ($nplvl || defined($snapshot)) {
	$mangled_ver .= '.'.$nplvl;
    }
}
($mtail = $tail) =~ tr/-/./;
$mangled_ver .= $mtail;

if ( $what eq 'h' ) {
    print  "#ifndef NASM_VERSION_H\n";
    print  "#define NASM_VERSION_H\n";
    printf "#define NASM_MAJOR_VER      %d\n", $nmaj;
    printf "#define NASM_MINOR_VER      %d\n", $nmin;
    printf "#define NASM_SUBMINOR_VER   %d\n", $nsmin;
    printf "#define NASM_PATCHLEVEL_VER %d\n", $nplvl;
    if (defined($snapshot)) {
	printf "#define NASM_SNAPSHOT       %d\n", $snapshot;
    }
    printf "#define NASM_VERSION_ID     0x%08x\n", $nasm_id;
    printf "#define NASM_VER            \"%s\"\n", $line;
    print  "#endif /* NASM_VERSION_H */\n";
} elsif ( $what eq 'mac' ) {
    print  "STD: version\n";
    printf "%%define __NASM_MAJOR__ %d\n", $nmaj;
    printf "%%define __NASM_MINOR__ %d\n", $nmin;
    printf "%%define __NASM_SUBMINOR__ %d\n", $nsmin;
    printf "%%define __NASM_PATCHLEVEL__ %d\n", $nplvl;
    if (defined($snapshot)) {
	printf "%%define __NASM_SNAPSHOT__ %d\n", $snapshot;
    }
    printf "%%define __NASM_VERSION_ID__ 0%08Xh\n", $nasm_id;
    printf "%%define __NASM_VER__ \"%s\"\n", $line;
} elsif ( $what eq 'sed' ) {
    printf "s/\@\@NASM_MAJOR\@\@/%d/g\n", $nmaj;
    printf "s/\@\@NASM_MINOR\@\@/%d/g\n", $nmin;
    printf "s/\@\@NASM_SUBMINOR\@\@/%d/g\n", $nsmin;
    printf "s/\@\@NASM_PATCHLEVEL\@\@/%d/g\n", $nplvl;
    printf "s/\@\@NASM_SNAPSHOT\@\@/%d/g\n", $snapshot;	# Possibly empty
    printf "s/\@\@NASM_VERSION_ID\@\@/%d/g\n", $nasm_id;
    printf "s/\@\@NASM_VERSION_XID\@\@/0x%08x/g\n", $nasm_id;
    printf "s/\@\@NASM_VER\@\@/%s/g\n", $line;
    printf "s/\@\@NASM_MANGLED_VER\@\@/%s/g\n", $mangled_ver;
} elsif ( $what eq 'make' ) {
    printf "NASM_VER=%s\n", $line;
    printf "NASM_MAJOR_VER=%d\n", $nmaj;
    printf "NASM_MINOR_VER=%d\n", $nmin;
    printf "NASM_SUBMINOR_VER=%d\n", $nsmin;
    printf "NASM_PATCHLEVEL_VER=%d\n", $nplvl;
} elsif ( $what eq 'nsis' ) {
    printf "!define VERSION \"%s\"\n", $line;
    printf "!define MAJOR_VER %d\n", $nmin;
    printf "!define MINOR_VER %d\n", $nmin;
    printf "!define SUBMINOR_VER %d\n", $nsmin;
    printf "!define PATCHLEVEL_VER %d\n", $nplvl;
} elsif ( $what eq 'id' ) {
    print $nasm_id, "\n";	 # Print ID in decimal
} elsif ( $what eq 'xid' ) {
    printf "0x%08x\n", $nasm_id; # Print ID in hexadecimal
} elsif ( $what eq 'docsrc' ) {
    printf "\\M{version}{%s}\n", $line;
    printf "\\M{subtitle}{version %s}\n", $line;
} else {
    die "$0: Unknown output: $what\n";
}

exit 0;
