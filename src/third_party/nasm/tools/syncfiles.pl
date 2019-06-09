#!/usr/bin/perl
## --------------------------------------------------------------------------
##   
##   Copyright 1996-2009 The NASM Authors - All Rights Reserved
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
# Sync the output file list between Makefiles
# Use the mkdep.pl parameters to get the filename syntax
#
# The first file is the source file; the other ones target.
#
%def_hints = ('object-ending' => '.o',
	      'path-separator' => '/',
	      'continuation' => "\\");

sub do_transform($$) {
    my($l, $h) = @_;
    my($ps) = $$h{'path-separator'};

    $l =~ s/\x01/$$h{'object-ending'}/g;
    $l =~ s/\x03/$$h{'continuation'}/g;

    if ($ps eq '') {
	# Remove the path separator and the preceeding directory
	$l =~ s/[^\s\=]*\x02//g;
    } else {
	# Convert the path separator
	$l =~ s/\x02/$ps/g;
    }

    return $l;
}

undef %line_lists;

$first = 1;
$first_file = $ARGV[0];
die unless (defined($first_file));

foreach $file (@ARGV) {
    open(FILE, '<', $file) or die;

    # First, read the syntax hints
    %hints = %def_hints;
    while (defined($line = <FILE>)) {
	if ( $line =~ /^\s*\#\s*@([a-z0-9-]+):\s*\"([^\"]*)\"/ ) {
	    $hints{$1} = $2;
	}
    }

    # Read and process the file
    seek(FILE,0,0);
    @lines = ();
    undef $processing;
    while (defined($line = <FILE>)) {
	chomp $line;
	if (defined($processing)) {
	    if ($line =~ /^\#-- End ([^-\#]*[^-#\s]) --\#$/) {
		if ($1 ne $processing) {
		    die "$0: $file: Mismatched Begin and End lines (\"$processing\" -> \"$1\"\n";
		}
		push(@lines, $line."\n");
		undef $processing;
	    } elsif ($first) {
		my $xl = $line;
		my $oe = "\Q$hints{'object-ending'}";
		my $ps = "\Q$hints{'path-separator'}";
		my $cn = "\Q$hints{'continuation'}";

		$xl =~ s/${oe}(\s|$)/\x01$1/g;
		$xl =~ s/${ps}/\x02/g;
		$xl =~ s/${cn}$/\x03/;
		push(@{$line_lists{$processing}}, $xl);
		push(@lines, $line);
	    }
	} else {
	    push(@lines, $line."\n");
	    if ($line =~ '#-- Begin ([^-\#]*[^-#\s]) --#') {
		$processing = $1;
		if ($first) {
		    if (defined($line_lists{$processing})) {
			die "$0: $file: Repeated Begin block: $processing\n";
		    }
		    $line_lists{$processing} = [];
		} elsif (!$first) {
		    if (!defined($line_lists{$processing})) {
			die "$0: $file: Begin block without template\n";
		    }
		    push(@lines, "# Edit in $first_file, not here!\n");
		    foreach $l (@{$line_lists{$processing}}) {
			push(@lines, do_transform($l, \%hints)."\n");
		    }
		}
	    }
	}
    }
    close(FILE);

    # Write the file back out
    if (!$first) {
	open(FILE, '>', $file) or die;
	print FILE @lines;
	close(FILE);
    }

    undef @lines;
    $first = 0;
}
