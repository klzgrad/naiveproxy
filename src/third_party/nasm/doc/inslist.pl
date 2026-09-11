#!/usr/bin/perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright 1996-2024 The NASM Authors - All Rights Reserved

#
# inslist.pl   produce inslist.src
#

print STDERR "Reading insns.dat...\n";

@args   = ();
undef $output;
foreach $arg ( @ARGV ) {
    if ( $arg =~ /^\-/ ) {
	if  ( $arg =~ /^\-([adins])$/ ) {
	    $output = $1;
	} else {
	    die "$0: Unknown option: ${arg}\n";
	}
    } else {
	push (@args, $arg);
    }
}

$fname = "../insns.dat" unless $fname = shift(@args);
open (F, '<', $fname) or die "$0: $fname, $!\n";
$ofile = "insns.src" unless $ofile = shift(@args);
open(S, '>', $ofile) or die "$0: $ofile: $!\n";
print STDERR "Writing $ofile...\n";
$line = 0;
$insns = 0;
while (<F>) {
  $line++;
  next if (/^\s*$/);		# blank lines
  if ( /^\s*;/ )		# comments
  {
    if ( /^\s*;\#\s*(.+)/ )	# section subheader
    {
      print S "\n\\S{} $1\n\n";
    }
    next;
  }
  chomp;
  unless (/^\s*(\S+)\s+(\S+)\s+(\S+|\[.*\])\s+(\S+)\s*$/) {
      warn "line $line does not contain four fields\n";
      next;
  }
  my @entry = ($1, $2, $3, $4);

  $entry[1] =~ s/ignore//;
  $entry[1] =~ s/void//;

  my @flags = split(/,/, $entry[3]);
  my @nflags;
  undef $isavx512;
  undef @avx512fl;
  for my $fl (@flags) {
      next if ($fl =~ /^(ignore|SB|SM|SM2|SQ|AR2|FUTURE)$/);

      if ($fl =~ /^AVX512(.*)$/) {
	  $isavx512 = 1;
	  push(@avx512fl, $1) unless ($1 eq '');
      } else {
	  push(@nflags,$fl);
      }
  }

  if ($isavx512) {
      unshift(@nflags, "AVX512".join('/', @avx512fl));
  }

  printf S "\\c %-16s %-24s %s\n",$entry[0],$entry[1], join(',', @nflags);
  $insns++;
}
print S "\n";
close S;
close F;
printf STDERR "Done: %d instructions\n", $insns;
