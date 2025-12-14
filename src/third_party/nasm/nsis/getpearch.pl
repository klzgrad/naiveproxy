#!/usr/bin/perl
#
# Get the appropriate variables to make an NSIS installer file
# based on the PE architecture of a specific file
#

use strict;
use bytes;

my %archnames = (
    0x01de => 'am33',
    0x8664 => 'x64',
    0x01c0 => 'arm32',
    0x01c4 => 'thumb',
    0xaa64 => 'arm64',
    0x0ebc => 'efi',
    0x014c => 'x86',
    0x0200 => 'ia64',
    0x9041 => 'm32r',
    0x0266 => 'mips16',
    0x0366 => 'mips',
    0x0466 => 'mips16',
    0x01f0 => 'powerpc',
    0x01f1 => 'powerpc',
    0x0166 => 'mips',
    0x01a2 => 'sh3',
    0x01a3 => 'sh3',
    0x01a6 => 'sh4',
    0x01a8 => 'sh5',
    0x01c2 => 'arm32',
    0x0169 => 'wcemipsv2'
);

my ($file) = @ARGV;
open(my $fh, '<', $file)
    or die "$0: cannot open file: $file: $!\n";

read($fh, my $mz, 2);
exit 1 if ($mz ne 'MZ');

exit 0 unless (seek($fh, 0x3c, 0));
exit 0 unless (read($fh, my $pe_offset, 4) == 4);
$pe_offset = unpack("V", $pe_offset);

exit 1 unless (seek($fh, $pe_offset, 0));
read($fh, my $pe, 4);
exit 1 unless ($pe eq "PE\0\0");

exit 1 unless (read($fh, my $arch, 2) == 2);
$arch = $archnames{unpack("v", $arch)};
if (defined($arch)) {
    print "!define ARCH ${arch}\n";
}

exit 1 unless (seek($fh, 14, 1));
exit 1 unless (read($fh, my $auxheaderlen, 2) == 2);
exit 1 unless (unpack("v", $auxheaderlen) >= 2);

exit 1 unless (seek($fh, 2, 1));
exit 1 unless (read($fh, my $petype, 2) == 2);
$petype = unpack("v", $petype);
if ($petype == 0x010b) {
    # It is a 32-bit PE32 file
    print "!define BITS 32\n";
    print "!define GLOBALINSTDIR \$PROGRAMFILES\n";
} elsif ($petype == 0x020b) {
    # It is a 64-bit PE32+ file
    print "!define BITS 64\n";
    print "!define GLOBALINSTDIR \$PROGRAMFILES64\n";
} else {
    # No idea...
    exit 1;
}

close($fh);
exit 0;
