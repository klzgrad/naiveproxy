#!/usr/bin/perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright 2025 The NASM Authors - All Rights Reserved

#
# Convert ultra-verbose in-file copyright statements to SPDX tags;
# strip trailing whitespace while we are at it...
#
use strict;
use integer;

# Strip excessive comment barriers for the purpose of matching
sub stripcom($) {
    my($l) = @_;

    $l =~ s/^(\S+) -+( \*)?$/$1/;
    $l =~ s/^ \* -+( \*\/)$/$1/; # Tail comment in C block
    return $1;
}

foreach my $file (@ARGV) {
    open(my $in, '<', $file) or die "$0: $file: $!\n";

    my @lines = ();
    my $copy;
    my $ctail;
    my $cpfx;
    my $modified = 0;

    while (defined(my $ll = <$in>)) {
	(my $l = $ll) =~ s/\s+$//;
	$modified = 1 if ($l."\n" ne $ll);

	if ($l =~ /^(\s*\S+)\s+(Copyright [1-2].*)?$/) {
	    $copy = $2;
	    $ctail = $cpfx = $1;
	    $ctail = ' */' if ($cpfx eq ' *'); # C-style block comment
	} elsif (defined($ctail) && (stripcom($l) eq $ctail)) {
	    # End of copyright comment. Walk backwards through the
	    # lines looking for the beginning.
	    while (scalar(@lines) && $lines[-1] =~ /^\Q$cpfx\E(\s.*)?/) {
		pop(@lines);
	    }

	    # Now the first line left is the start of the comment body;
	    # for line comments the entire comment is gone, but for a
	    # C-style block comment the comment start marker remains.

	    my $npfx = $cpfx.' ';
	    $npfx = '# ' if ($npfx eq '## '); # Change ## to #
	    my $nsuf = '';

	    # Change C block comments to inline comments
	    if ($ctail eq ' */') {
		pop(@lines) if ($lines[-1] =~ /^\/\*/);
		$npfx = '/* ';
		$nsuf = ' */';
	    }
	    my @hdr = ();
	    # Shebang and emacs mode lines should be left at the top
	    while (scalar(@lines) && $lines[0] =~ /^(?:\#\!|.*\-\*\-)/) {
		push(@hdr, shift(@lines));
	    }
	    push(@hdr, $npfx.'SPDX-License-Identifier: BSD-2-Clause'.$nsuf);
	    push(@hdr, $npfx.$copy.$nsuf);
	    unshift(@lines, @hdr);
	    undef $ctail;
	    $modified = 1;
	    next;
	}

	push(@lines, $l) if (defined($l));
    }
    close($in);

    while (scalar(@lines) && $lines[-1] =~ /^\s*$/) {
	pop(@lines);
	$modified = 1;
    }
    next unless ($modified);

    open(my $out, '>', $file) or die "$0: $file: $!\n";
    print $out map { "$_\n" } @lines;
    close($out);
}
exit 0;
