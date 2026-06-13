#!/usr/bin/perl

use strict;
use integer;

my $bw = 1;
my @errs = ();
foreach my $errf (@ARGV) {
    if (open(my $e, '<', $errf)) {
	while (defined(my $l = <$e>)) {
	    if ($l =~ /^.*?\:([0-9]+)\:/) {
		$errs[$1] |= $bw;
	    }
	}
	close($e);
    }
    $bw <<= 1;
}

my $ln = 0;

$ln++; print "%pragma list options -befms\n";
$ln++; print "%ifndef ERR\n";
$ln++; print " %define ERR 0\n";
$ln++; print "%endif\n";

my @nots = ('');

for (my $cc = 1; $cc < 7; $cc++) {
    my $ss = 'no';
    $ss .= 'w' if ($cc & 1);
    $ss .= 'd' if ($cc & 2);
    $ss .= 'q' if ($cc & 4);
    $ln++; print "%macro $ss 1+.nolist\n";
    $ln++; printf " %%if ERR || !(__BITS__ & 0x%02x)\n", $cc << 4;
    $ln++; print "\t%1\n";
    $ln++; print " %endif\n";
    $ln++; print "%endmacro\n";
    push(@nots, "$ss");
}
$ln++; print "%macro bogus 1+.nolist\n";
$ln++; printf " %%if ERR\n";
$ln++; print "\t%1\n";
$ln++; print " %endif\n";
$ln++; print "%endmacro\n";
push(@nots, 'bogus');

$ln++; print "\n";

$ln++; print "\tsection text1\n";
$ln++; print "top:\n";
$ln++; print "\ttimes 128 nop\n";
$ln++; print "\n";

foreach my $insn ('jmp', 'call', 'jz', 'jcxz', 'jecxz', 'jrcxz',
		  'loop', 'loope', 'loopne') {
    $ln++; print "here_$insn:\n";

    foreach my $tgt ('$', 'top', 'there') {
	foreach my $str ('', 'strict') {
	    foreach my $sz ('', 'byte', 'word', 'dword', 'qword') {
		foreach my $o ('', 'o16', 'o32', 'o64') {
		    foreach my $sn ('', 'short', 'near') {
			my $is_short =
			    ($sn eq 'short' || $insn =~ /^(j\w?cxz|loop\w*)$/);

			$ln++;
			$errs[$ln] |= 3 if ($sz eq 'qword' || $o eq 'o64'
					    || $insn eq 'jrcxz');
			$errs[$ln] |= 4 if ($sz =~ /^d?word$'/ || $o =~ /^o(16|32)$/ ||
			    $insn eq 'jcxz');
			if (($sz eq 'word' && $o =~ /^o(32|64)$/) ||
			    ($sz eq 'dword' && $o =~ /^o(16|64)$/) ||
			    ($sz eq 'qword' && $o =~ /^o(16|32)$/) ||
			    ($sz eq 'byte') ||
			    ($is_short &&
			     ($sn eq 'near' || $tgt ne '$' || $insn eq 'call'))) {
			    $errs[$ln] |= 7;
			}

			my $is_short =

			printf "  %-5s %s\n",
			    $nots[$errs[$ln]],
			    join(' ', grep { $_ ne '' }
				 ($o,$insn,$str,$sz,$sn,$tgt));
		    }
		}
	    }
	}
    }
}

$ln++; print "\n";
$ln++; print "\tsection text2\n";
$ln++; print "there:\n";
$ln++; print "\tret\n";
