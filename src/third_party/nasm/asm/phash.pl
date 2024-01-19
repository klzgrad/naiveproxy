#!/usr/bin/perl
## --------------------------------------------------------------------------
##   
##   Copyright 1996-2009 the NASM Authors - All rights reserved.
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
# Perfect Minimal Hash Generator written in Perl, which produces
# C output.
#

require 'phash.ph';

#
# Read input file
#
sub read_input() {
    my $key,$val;
    my %out;
    my $x = 0;

    while (defined($l = <STDIN>)) {
	chomp $l;
	$l =~ s/\s*(\#.*|)$//;

	next if ($l eq '');

	if ($l =~ /^([^=]+)\=([^=]+)$/) {
	    $out{$1} = $2;
	    $x = $2;
	} else {
	    $out{$l} = $x;
	}
	$x++;
    }

    return %out;
}

#
# Main program
#
sub main() {
    my $n;
    my %data;
    my @hashinfo;
    my $x, $i;

    %data = read_input();
    @hashinfo = gen_perfect_hash(\%data);

    if (!@hashinfo) {
	die "$0: no hash found\n";
    }

    verify_hash_table(\%data, \@hashinfo);

    ($n, $sv, $f1, $f2, $g) = @hashinfo;

    print "static int HASHNAME_fg1[$n] =\n";
    print "{\n";
    for ($i = 0; $i < $n; $i++) {
	print "\t", ${$g}[${$f1}[$i]], "\n";
    }
    print "};\n\n";

    print "static int HASHNAME_fg2[$n] =\n";
    print "{\n";
    for ($i = 0; $i < $n; $i++) {
	print "\t", ${$g}[${$f2}[$i]], "\n";
    }
    print "};\n\n";

    print "struct p_hash HASHNAME =\n";
    print "{\n";
    print "\t$n\n";
    print "\t$sv\n";
    print "\tHASHNAME_fg1,\n";
    print "\tHASHNAME_fg2,\n";
    print "};\n";
}

main();
