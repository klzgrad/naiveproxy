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

$fname = "../insns.dat" unless $fname = $args[0];
open (F, '<', $fname) || die "unable to open $fname";
print STDERR "Writing inslist.src...\n";
open S, '>', 'inslist.src';
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

