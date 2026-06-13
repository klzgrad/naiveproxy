#!/usr/bin/perl

use integer;

while (<>) {
    chomp;
    print $_;
    if (!/\#/ &&
	# VEX/XOP is implicit so no need to clutter
	# the table with it...
	!/\b(rex2|vex|evex|xop)\b/ &&
	!/\bNOAPX\b/ &&
	!/\bNOLONG\b/ &&
	# Maps 2+ are not REX2 encodable, this is implicit
	!/\b0f3[a8]\b/ &&
	!/\b(m|map)([2-9]|[1][0-9])\b/ &&
	((!/\b0f\b/ &&
	  !/\b[^47ae][0-9a-f] [47ae][0-9a-f]\b/ &&
	  /\b[47ae][0-9a-f]\b/) ||
	 /\b0f [38][0-9a-f]\b/ ||
	 /^(XSAVE|XRSTOR)/)) {
	print ",NOAPX";
    }
    print "\n";
}
