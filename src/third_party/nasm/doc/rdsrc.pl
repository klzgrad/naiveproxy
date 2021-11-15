#!/usr/bin/perl
## --------------------------------------------------------------------------
##
##   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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


# Read the source-form of the NASM manual and generate the various
# output forms.

# TODO:
#
# Ellipsis support would be nice.

# Source-form features:
# ---------------------
#
# Bullet \b
#   Bullets the paragraph. Rest of paragraph is indented to cope. In
#   HTML, consecutive groups of bulleted paragraphs become unordered
#   lists.
#
# Indent \>
#   Indents the paragraph equvalently to a bulleted paragraph.  In HTML,
#   an indented paragraph following a bulleted paragraph is included in the
#   same list item.
#
# Blockquote \q
#   Marks the paragraph as a block quote.
#
# Emphasis \e{foobar}
#   produces `_foobar_' in text and italics in HTML, PS, RTF
#
# Inline code \c{foobar}
#   produces ``foobar'' in text, and fixed-pitch font in HTML, PS, RTF
#
# Display code
# \c  line one
# \c   line two
#   produces fixed-pitch font where appropriate, and doesn't break
#   pages except sufficiently far into the middle of a display.
#
# Chapter, header and subheader
# \C{intro} Introduction
# \H{whatsnasm} What is NASM?
# \S{free} NASM Is Free
#   dealt with as appropriate. Chapters begin on new sides, possibly
#   even new _pages_. (Sub)?headers are good places to begin new
#   pages. Just _after_ a (sub)?header isn't.
#   The keywords can be substituted with \K and \k.
#
# Keyword \K{cintro} \k{cintro}
#   Expands to `Chapter 1', `Section 1.1', `Section 1.1.1'. \K has an
#   initial capital whereas \k doesn't. In HTML, will produce
#   hyperlinks.
#
# Web link \W{http://foobar/}{text} or \W{mailto:me@here}\c{me@here}
#   the \W prefix is ignored except in HTML; in HTML the last part
#   becomes a hyperlink to the first part.
#
# Literals \{ \} \\
#   In case it's necessary, they expand to the real versions.
#
# Nonbreaking hyphen \-
#   Need more be said?
#
# Source comment \#
#   Causes everything after it on the line to be ignored by the
#   source-form processor.
#
# Indexable word \i{foobar} (or \i\e{foobar} or \i\c{foobar}, equally)
#   makes word appear in index, referenced to that point
#   \i\c comes up in code style even in the index; \i\e doesn't come
#   up in emphasised style.
#
# Indexable non-displayed word \I{foobar} or \I\c{foobar}
#   just as \i{foobar} except that nothing is displayed for it
#
# Index rewrite
# \IR{foobar} \c{foobar} operator, uses of
#   tidies up the appearance in the index of something the \i or \I
#   operator was applied to
#
# Index alias
# \IA{foobar}{bazquux}
#   aliases one index tag (as might be supplied to \i or \I) to
#   another, so that \I{foobar} has the effect of \I{bazquux}, and
#   \i{foobar} has the effect of \I{bazquux}foobar
#
# Metadata
# \M{key}{something}
#   defines document metadata, such as authorship, title and copyright;
#   different output formats use this differently.
#
# Include subfile
# \&{filename}
#  Includes filename. Recursion is allowed.
#

use File::Spec;

@include_path = ();
$out_path = File::Spec->curdir();

while ($ARGV[0] =~ /^-/) {
    my $opt = shift @ARGV;
    if ($opt eq '-d') {
	$diag = 1;
    } elsif ($opt =~ /^\-[Ii](.*)$/) {
	push(@include_path, $1);
    } elsif ($opt =~ /^\-[Oo](.*)$/) {
	$out_path = $1;
    }
}

$out_format = shift(@ARGV);
@files = @ARGV;
@files = ('-') unless(scalar(@files));

$| = 1;

$tstruct_previtem = $node = "Top";
$nodes = ($node);
$tstruct_level{$tstruct_previtem} = 0;
$tstruct_last[$tstruct_level{$tstruct_previtem}] = $tstruct_previtem;
$MAXLEVEL = 10;  # really 3, but play safe ;-)

# Read the file; pass a paragraph at a time to the paragraph processor.
print "Reading input...";
$pname = "para000000";
@pnames = @pflags = ();
$para = undef;
foreach $file (@files) {
  &include($file);
}
&got_para($para);
print "done.\n";

# Now we've read in the entire document and we know what all the
# heading keywords refer to. Go through and fix up the \k references.
print "Fixing up cross-references...";
&fixup_xrefs;
print "done.\n";

# Sort the index tags, according to the slightly odd order I've decided on.
print "Sorting index tags...";
&indexsort;
print "done.\n";

# Make output directory if necessary
mkdir($out_path);

if ($diag) {
  print "Writing index-diagnostic file...";
  &indexdiag;
  print "done.\n";
}

# OK. Write out the various output files.
if ($out_format eq 'txt') {
    print "Producing text output: ";
    &write_txt;
    print "done.\n";
} elsif ($out_format eq 'html') {
    print "Producing HTML output: ";
    &write_html;
    print "done.\n";
} elsif ($out_format eq 'dip') {
    print "Producing Documentation Intermediate Paragraphs: ";
    &write_dip;
    print "done.\n";
} else {
    die "$0: unknown output format: $out_format\n";
}

sub untabify($) {
  my($s) = @_;
  my $o = '';
  my($c, $i, $p);

  $p = 0;
  for ($i = 0; $i < length($s); $i++) {
    $c = substr($s, $i, 1);
    if ($c eq "\t") {
      do {
	$o .= ' ';
	$p++;
      } while ($p & 7);
    } else {
      $o .= $c;
      $p++;
    }
  }
  return $o;
}
sub read_line {
  local $_ = shift;
  $_ = &untabify($_);
  if (/\\& (\S+)/) {
     &include($1);
  } else {
     &get_para($_);
  }
}
sub get_para($_) {
  chomp;
  if (!/\S/ || /^\\(IA|IR|M)/) { # special case: \IA \IR \M imply new-paragraph
    &got_para($para);
    $para = undef;
  }
  if (/\S/) {
    s/(^|[^\\])\\#.*$/\1/; # strip comments
    $para .= " " . $_;
  }
}
sub include {
  my $name = shift;
  my $F;

  if ($name eq '-') {
    open($F, '<-');		# stdin
  } else {
    my $found = 0;
    foreach my $idir ( File::Spec->curdir, @include_path ) {
	my $fpath = File::Spec->catfile($idir, $name);
      if (open($F, '<', $fpath)) {
	$found = 1;
	last;
      }
    }
    die "Cannot open $name: $!\n" unless ($found);
  }
  while (defined($_ = <$F>)) {
     &read_line($_);
  }
  close($F);
}
sub got_para {
  local ($_) = @_;
  my $pflags = "", $i, $w, $l, $t;
  return if !/\S/;

  @$pname = ();

  # Strip off _leading_ spaces, then determine type of paragraph.
  s/^\s*//;
  $irewrite = undef;
  if (/^\\c[^{]/) {
    # A code paragraph. The paragraph-array will contain the simple
    # strings which form each line of the paragraph.
    $pflags = "code";
    while (/^\\c (([^\\]|\\[^c])*)(.*)$/) {
      $l = $1;
      $_ = $3;
      $l =~ s/\\\{/\{/g;
      $l =~ s/\\\}/}/g;
      $l =~ s/\\\\/\\/g;
      push @$pname, $l;
    }
    $_ = ''; # suppress word-by-word code
  } elsif (/^\\C/) {
    # A chapter heading. Define the keyword and allocate a chapter
    # number.
    $cnum++;
    $hnum = 0;
    $snum = 0;
    $xref = "chapter-$cnum";
    $pflags = "chap $cnum :$xref";
    die "badly formatted chapter heading: $_\n" if !/^\\C\{([^\}]*)\}\s*(.*)$/;
    $refs{$1} = "chapter $cnum";
    $node = "Chapter $cnum";
    &add_item($node, 1);
    $xrefnodes{$node} = $xref; $nodexrefs{$xref} = $node;
    $xrefs{$1} = $xref;
    $_ = $2;
    # the standard word-by-word code will happen next
  } elsif (/^\\A/) {
    # An appendix heading. Define the keyword and allocate an appendix
    # letter.
    $cnum++;
    $cnum = 'A' if $cnum =~ /[0-9]+/;
    $hnum = 0;
    $snum = 0;
    $xref = "appendix-$cnum";
    $pflags = "appn $cnum :$xref";
    die "badly formatted appendix heading: $_\n" if !/^\\A\{([^\}]*)}\s*(.*)$/;
    $refs{$1} = "appendix $cnum";
    $node = "Appendix $cnum";
    &add_item($node, 1);
    $xrefnodes{$node} = $xref; $nodexrefs{$xref} = $node;
    $xrefs{$1} = $xref;
    $_ = $2;
    # the standard word-by-word code will happen next
  } elsif (/^\\H/) {
    # A major heading. Define the keyword and allocate a section number.
    $hnum++;
    $snum = 0;
    $xref = "section-$cnum.$hnum";
    $pflags = "head $cnum.$hnum :$xref";
    die "badly formatted heading: $_\n" if !/^\\[HP]\{([^\}]*)\}\s*(.*)$/;
    $refs{$1} = "section $cnum.$hnum";
    $node = "Section $cnum.$hnum";
    &add_item($node, 2);
    $xrefnodes{$node} = $xref; $nodexrefs{$xref} = $node;
    $xrefs{$1} = $xref;
    $_ = $2;
    # the standard word-by-word code will happen next
  } elsif (/^\\S/) {
    # A sub-heading. Define the keyword and allocate a section number.
    $snum++;
    $xref = "section-$cnum.$hnum.$snum";
    $pflags = "subh $cnum.$hnum.$snum :$xref";
    die "badly formatted subheading: $_\n" if !/^\\S\{([^\}]*)\}\s*(.*)$/;
    $refs{$1} = "section $cnum.$hnum.$snum";
    $node = "Section $cnum.$hnum.$snum";
    &add_item($node, 3);
    $xrefnodes{$node} = $xref; $nodexrefs{$xref} = $node;
    $xrefs{$1} = $xref;
    $_ = $2;
    # the standard word-by-word code will happen next
  } elsif (/^\\IR/) {
    # An index-rewrite.
    die "badly formatted index rewrite: $_\n" if !/^\\IR\{([^\}]*)\}\s*(.*)$/;
    $irewrite = $1;
    $_ = $2;
    # the standard word-by-word code will happen next
  } elsif (/^\\IA/) {
    # An index-alias.
    die "badly formatted index alias: $_\n" if !/^\\IA\{([^\}]*)}\{([^\}]*)\}\s*$/;
    $idxalias{$1} = $2;
    return; # avoid word-by-word code
  } elsif (/^\\M/) {
    # Metadata
    die "badly formed metadata: $_\n" if !/^\\M\{([^\}]*)}\{([^\}]*)\}\s*$/;
    $metadata{$1} = $2;
    return; # avoid word-by-word code
  } elsif (/^\\([b\>q])/) {
    # An indented paragraph of some sort. Strip off the initial \b and let the
      # word-by-word code take care of the rest.
      my %ipar = (
	  'b' => 'bull',
	  '>' => 'indt',
	  'q' => 'bquo',
	  );
    $pflags = $ipar{$1};
    s/^\\[b\>q]\s*//;
  } else {
    # A normal paragraph. Just set $pflags: the word-by-word code does
    # the rest.
    $pflags = "norm";
  }

  # The word-by-word code: unless @$pname is already defined (which it
  # will be in the case of a code paragraph), split the paragraph up
  # into words and push each on @$pname.
  #
  # Each thing pushed on @$pname should have a two-character type
  # code followed by the text.
  #
  # Type codes are:
  # "n " for normal
  # "da" for an en dash
  # "dm" for an em desh
  # "es" for first emphasised word in emphasised bit
  # "e " for emphasised in mid-emphasised-bit
  # "ee" for last emphasised word in emphasised bit
  # "eo" for single (only) emphasised word
  # "c " for code
  # "k " for cross-ref
  # "kK" for capitalised cross-ref
  # "w " for Web link
  # "wc" for code-type Web link
  # "x " for beginning of resolved cross-ref; generates no visible output,
  #      and the text is the cross-reference code
  # "xe" for end of resolved cross-ref; text is same as for "x ".
  # "i " for point to be indexed: the text is the internal index into the
  #      index-items arrays
  # "sp" for space
  while (/\S/) {
    s/^\s*//, push @$pname, "sp" if /^\s/;
    $indexing = $qindex = 0;
    if (/^(\\[iI])?\\c/) {
      $qindex = 1 if $1 eq "\\I";
      $indexing = 1, s/^\\[iI]// if $1;
      s/^\\c//;
      die "badly formatted \\c: \\c$_\n" if !/\{(([^\\}]|\\.)*)\}(.*)$/;
      $w = $1;
      $_ = $3;
      $w =~ s/\\\{/\{/g;
      $w =~ s/\\\}/\}/g;
      $w =~ s/\\-/-/g;
      $w =~ s/\\\\/\\/g;
      (push @$pname,"i"),$lastp = $#$pname if $indexing;
      push @$pname,"c $w" if !$qindex;
      $$pname[$lastp] = &addidx($node, $w, "c $w") if $indexing;
    } elsif (/^\\[iIe]/) {
      /^(\\[iI])?(\\e)?/;
      $emph = 0;
      $qindex = 1 if $1 eq "\\I";
      $indexing = 1, $type = "\\i" if $1;
      $emph = 1, $type = "\\e" if $2;
      s/^(\\[iI])?(\\e?)//;
      die "badly formatted $type: $type$_\n" if !/\{(([^\\}]|\\.)*)\}(.*)$/;
      $w = $1;
      $_ = $3;
      $w =~ s/\\\{/\{/g;
      $w =~ s/\\\}/\}/g;
      $w =~ s/\\-/-/g;
      $w =~ s/\\\\/\\/g;
      $t = $emph ? "es" : "n ";
      @ientry = ();
      (push @$pname,"i"),$lastp = $#$pname if $indexing;
      foreach $i (split /\s+/,$w) {  # \e and \i can be multiple words
        push @$pname,"$t$i","sp" if !$qindex;
	($ii=$i) =~ tr/A-Z/a-z/, push @ientry,"n $ii","sp" if $indexing;
	$t = $emph ? "e " : "n ";
      }
      $w =~ tr/A-Z/a-z/, pop @ientry if $indexing;
      $$pname[$lastp] = &addidx($node, $w, @ientry) if $indexing;
      pop @$pname if !$qindex; # remove final space
      if (substr($$pname[$#$pname],0,2) eq "es" && !$qindex) {
        substr($$pname[$#$pname],0,2) = "eo";
      } elsif ($emph && !$qindex) {
        substr($$pname[$#$pname],0,2) = "ee";
      }
    } elsif (/^\\[kK]/) {
      $t = "k ";
      $t = "kK" if /^\\K/;
      s/^\\[kK]//;
      die "badly formatted \\k: \\k$_\n" if !/\{([^\}]*)\}(.*)$/;
      $_ = $2;
      push @$pname,"$t$1";
    } elsif (/^\\W/) {
      s/^\\W//;
      die "badly formatted \\W: \\W$_\n"
          if !/\{([^\}]*)\}(\\i)?(\\c)?\{(([^\\}]|\\.)*)\}(.*)$/;
      $l = $1;
      $w = $4;
      $_ = $6;
      $t = "w ";
      $t = "wc" if $3 eq "\\c";
      $indexing = 1 if $2;
      $w =~ s/\\\{/\{/g;
      $w =~ s/\\\}/\}/g;
      $w =~ s/\\-/-/g;
      $w =~ s/\\\\/\\/g;
      (push @$pname,"i"),$lastp = $#$pname if $indexing;
      push @$pname,"$t<$l>$w";
      $$pname[$lastp] = &addidx($node, $w, "c $w") if $indexing;
    } else {
      die "what the hell? $_\n" if !/^(([^\s\\\-]|\\[\\{}\-])*-?)(.*)$/;
      die "painful death! $_\n" if !length $1;
      $w = $1;
      $_ = $3;
      $w =~ s/\\\{/\{/g;
      $w =~ s/\\\}/\}/g;
      $w =~ s/\\-/-/g;
      $w =~ s/\\\\/\\/g;
      if ($w eq '--') {
	  push @$pname, 'dm';
      } elsif ($w eq '-') {
        push @$pname, 'da';
      } else {
        push @$pname,"n $w";
      }
    }
  }
  if ($irewrite ne undef) {
    &addidx(undef, $irewrite, @$pname);
    @$pname = ();
  } else {
    push @pnames, $pname;
    push @pflags, $pflags;
    $pname++;
  }
}

sub addidx {
  my ($node, $text, @ientry) = @_;
  $text = $idxalias{$text} || $text;
  if ($node eq undef || !$idxmap{$text}) {
    @$ientry = @ientry;
    $idxmap{$text} = $ientry;
    $ientry++;
  }
  if ($node) {
    $idxnodes{$node,$text} = 1;
    return "i $text";
  }
}

sub indexsort {
  my $iitem, $ientry, $i, $piitem, $pcval, $cval, $clrcval;

  @itags = map { # get back the original data as the 1st elt of each list
             $_->[0]
	   } sort { # compare auxiliary (non-first) elements of lists
	     $a->[1] cmp $b->[1] ||
	     $a->[2] cmp $b->[2] ||
	     $a->[0] cmp $b->[0]
           } map { # transform array into list of 3-element lists
	     my $ientry = $idxmap{$_};
	     my $a = substr($$ientry[0],2);
	     $a =~ tr/A-Za-z0-9//cd;
	     [$_, uc($a), substr($$ientry[0],0,2)]
	   } keys %idxmap;

  # Having done that, check for comma-hood.
  $cval = 0;
  foreach $iitem (@itags) {
    $ientry = $idxmap{$iitem};
    $clrcval = 1;
    $pcval = $cval;
    FL:for ($i=0; $i <= $#$ientry; $i++) {
      if ($$ientry[$i] =~ /^(n .*,)(.*)/) {
        $$ientry[$i] = $1;
	splice @$ientry,$i+1,0,"n $2" if length $2;
	$commapos{$iitem} = $i+1;
	$cval = join("\002", @$ientry[0..$i]);
	$clrcval = 0;
	last FL;
      }
    }
    $cval = undef if $clrcval;
    $commanext{$iitem} = $commaafter{$piitem} = 1
      if $cval and ($cval eq $pcval);
    $piitem = $iitem;
  }
}

sub indexdiag {
  my $iitem,$ientry,$w,$ww,$foo,$node;
  open INDEXDIAG, '>', File::Spec->catfile($out_path, 'index.diag');
  foreach $iitem (@itags) {
    $ientry = $idxmap{$iitem};
    print INDEXDIAG "<$iitem> ";
    foreach $w (@$ientry) {
      $ww = &word_txt($w);
      print INDEXDIAG $ww unless $ww eq "\001";
    }
    print INDEXDIAG ":";
    $foo = " ";
    foreach $node (@nodes) {
      (print INDEXDIAG $foo,$node), $foo = ", " if $idxnodes{$node,$iitem};
    }
    print INDEXDIAG "\n";
  }
  close INDEXDIAG;
}

sub fixup_xrefs {
  my $pname, $p, $i, $j, $k, $caps, @repl;

  for ($p=0; $p<=$#pnames; $p++) {
    next if $pflags[$p] eq "code";
    $pname = $pnames[$p];
    for ($i=$#$pname; $i >= 0; $i--) {
      if ($$pname[$i] =~ /^k/) {
        $k = $$pname[$i];
        $caps = ($k =~ /^kK/);
	$k = substr($k,2);
        $repl = $refs{$k};
	die "undefined keyword `$k'\n" unless $repl;
	substr($repl,0,1) =~ tr/a-z/A-Z/ if $caps;
	@repl = ();
	push @repl,"x $xrefs{$k}";
	foreach $j (split /\s+/,$repl) {
	  push @repl,"n $j";
	  push @repl,"sp";
	}
	pop @repl; # remove final space
	push @repl,"xe$xrefs{$k}";
	splice @$pname,$i,1,@repl;
      }
    }
  }
}

sub write_txt {
  # This is called from the top level, so I won't bother using
  # my or local.

  # Open file.
  print "writing file...";
  open TEXT, '>', File::Spec->catfile($out_path, 'nasmdoc.txt');
  select TEXT;

  # Preamble.
  $title = $metadata{'title'};
  $spaces = ' ' x ((75-(length $title))/2);
  ($underscore = $title) =~ s/./=/g;
  print "$spaces$title\n$spaces$underscore\n";

  for ($para = 0; $para <= $#pnames; $para++) {
    $pname = $pnames[$para];
    $pflags = $pflags[$para];
    $ptype = substr($pflags,0,4);

    print "\n"; # always one of these before a new paragraph

    if ($ptype eq "chap") {
      # Chapter heading. "Chapter N: Title" followed by a line of
      # minus signs.
      $pflags =~ /chap (.*) :(.*)/;
      $title = "Chapter $1: ";
      foreach $i (@$pname) {
        $ww = &word_txt($i);
        $title .= $ww unless $ww eq "\001";
      }
      print "$title\n";
      $title =~ s/./-/g;
      print "$title\n";
    } elsif ($ptype eq "appn") {
      # Appendix heading. "Appendix N: Title" followed by a line of
      # minus signs.
      $pflags =~ /appn (.*) :(.*)/;
      $title = "Appendix $1: ";
      foreach $i (@$pname) {
        $ww = &word_txt($i);
        $title .= $ww unless $ww eq "\001";
      }
      print "$title\n";
      $title =~ s/./-/g;
      print "$title\n";
    } elsif ($ptype eq "head" || $ptype eq "subh") {
      # Heading or subheading. Just a number and some text.
      $pflags =~ /.... (.*) :(.*)/;
      $title = sprintf "%6s ", $1;
      foreach $i (@$pname) {
        $ww = &word_txt($i);
        $title .= $ww unless $ww eq "\001";
      }
      print "$title\n";
    } elsif ($ptype eq "code") {
      # Code paragraph. Emit each line with a seven character indent.
      foreach $i (@$pname) {
        warn "code line longer than 68 chars: $i\n" if length $i > 68;
        print ' 'x7, $i, "\n";
      }
    } elsif ($ptype =~ /^(norm|bull|indt|bquo)$/) {
      # Ordinary paragraph, optionally indented. We wrap, with ragged
      # 75-char right margin and either 7 or 11 char left margin
      # depending on bullets.
      if ($ptype ne 'norm') {
	  $line = ' 'x7 . (($ptype eq 'bull') ? '(*) ' : '    ');
	  $next = ' 'x11;
      } else {
        $line = $next = ' 'x7;
      }
      @a = @$pname;
      $wd = $wprev = '';
      do {
        do { $w = &word_txt(shift @a) } while $w eq "\001"; # nasty hack
	$wd .= $wprev;
	if ($wprev =~ /-$/ || $w eq ' ' || $w eq '' || $w eq undef) {
	  if (length ($line . $wd) > 75) {
	    $line =~ s/\s*$//; # trim trailing spaces
	    print "$line\n";
	    $line = $next;
	    $wd =~ s/^\s*//; # trim leading spaces
	  }
	  $line .= $wd;
	  $wd = '';
	}
	$wprev = $w;
      } while ($w ne '' && $w ne undef);
      if ($line =~ /\S/) {
	$line =~ s/\s*$//; # trim trailing spaces
	print "$line\n";
      }
    }
  }

  # Close file.
  select STDOUT;
  close TEXT;
}

sub word_txt {
  my ($w) = @_;
  my $wtype, $wmajt;

  return undef if $w eq '' || $w eq undef;
  $wtype = substr($w,0,2);
  $wmajt = substr($wtype,0,1);
  $w = substr($w,2);
  $w =~ s/<.*>// if $wmajt eq "w"; # remove web links
  if ($wmajt eq "n" || $wtype eq "e " || $wtype eq "w ") {
    return $w;
  } elsif ($wtype eq "sp") {
    return ' ';
  } elsif ($wtype eq 'da' || $wtype eq 'dm') {
    return '-';
  } elsif ($wmajt eq "c" || $wtype eq "wc") {
    return "`${w}'";
  } elsif ($wtype eq "es") {
    return "_${w}";
  } elsif ($wtype eq "ee") {
    return "${w}_";
  } elsif ($wtype eq "eo") {
    return "_${w}_";
  } elsif ($wmajt eq "x" || $wmajt eq "i") {
    return "\001";
  } else {
    die "panic in word_txt: $wtype$w\n";
  }
}

sub write_html {
  # This is called from the top level, so I won't bother using
  # my or local.

  # Write contents file. Just the preamble, then a menu of links to the
  # separate chapter files and the nodes therein.
  print "writing contents file...";
  open TEXT, '>', File::Spec->catfile($out_path, 'nasmdoc0.html');
  select TEXT;
  &html_preamble(0);
  print "<p>This manual documents NASM, the Netwide Assembler: an assembler\n";
  print "targetting the Intel x86 series of processors, with portable source.\n</p>";
  print "<div class=\"toc\">\n";
  $level = 0;
  for ($node = $tstruct_next{'Top'}; $node; $node = $tstruct_next{$node}) {
      my $lastlevel = $level;
      while ($tstruct_level{$node} < $level) {
	  print "</li>\n</ol>\n";
	  $level--;
      }
      while ($tstruct_level{$node} > $level) {
	  print "<ol class=\"toc", ++$level, "\">\n";
      }
      if ($lastlevel >= $level) {
	  print "</li>\n";
      }
      $level = $tstruct_level{$node};
      if ($level == 1) {
      # Invent a file name.
	  ($number = lc($xrefnodes{$node})) =~ s/.*-//;
	  $fname="nasmdocx.html";
	  substr($fname,8 - length $number, length $number) = $number;
	  $html_fnames{$node} = $fname;
	  $link = $fname;
      } else {
	  # Use the preceding filename plus a marker point.
	  $link = $fname . "#$xrefnodes{$node}";
      }
      $title = '';
      $pname = $tstruct_pname{$node};
      foreach $i (@$pname) {
	  $ww = &word_html($i);
	  $title .= $ww unless $ww eq "\001";
      }
      print "<li class=\"toc${level}\">\n";
      print "<span class=\"node\">$node: </span><a href=\"$link\">$title</a>\n";
  }
  while ($level--) {
      print "</li>\n</ol>\n";
  }
  print "</div>\n";
  print "</body>\n";
  print "</html>\n";
  select STDOUT;
  close TEXT;

  # Open a null file, to ensure output (eg random &html_jumppoints calls)
  # goes _somewhere_.
  print "writing chapter files...";
  open TEXT, '>', File::Spec->devnull();
  select TEXT;
  undef $html_nav_last;
  undef $html_nav_next;

  $in_list = 0;
  $in_bquo = 0;
  $in_code = 0;

  for ($para = 0; $para <= $#pnames; $para++) {
    $pname = $pnames[$para];
    $pflags = $pflags[$para];
    $ptype = substr($pflags,0,4);

    $in_code = 0, print "</pre>\n" if ($in_code && $ptype ne 'code');
    $in_list = 0, print "</li>\n</ul>\n" if ($in_list && $ptype !~ /^(bull|indt|code)$/);
    $in_bquo = 0, print "</blockquote>\n" if ($in_bquo && $ptype ne 'bquo');

    $endtag = '';

    if ($ptype eq "chap") {
      # Chapter heading. Begin a new file.
      $pflags =~ /chap (.*) :(.*)/;
      $title = "Chapter $1: ";
      $xref = $2;
      &html_postamble; select STDOUT; close TEXT;
      $html_nav_last = $chapternode;
      $chapternode = $nodexrefs{$xref};
      $html_nav_next = $tstruct_mnext{$chapternode};
      open(TEXT, '>', File::Spec->catfile($out_path, $html_fnames{$chapternode}));
      select TEXT;
      &html_preamble(1);
      foreach $i (@$pname) {
        $ww = &word_html($i);
        $title .= $ww unless $ww eq "\001";
      }
      $h = "<h2 id=\"$xref\">$title</h2>\n";
      print $h; print FULL $h;
    } elsif ($ptype eq "appn") {
      # Appendix heading. Begin a new file.
      $pflags =~ /appn (.*) :(.*)/;
      $title = "Appendix $1: ";
      $xref = $2;
      &html_postamble; select STDOUT; close TEXT;
      $html_nav_last = $chapternode;
      $chapternode = $nodexrefs{$xref};
      $html_nav_next = $tstruct_mnext{$chapternode};
      open(TEXT, '>', File::Spec->catfile($out_path, $html_fnames{$chapternode}));
      select TEXT;
      &html_preamble(1);
      foreach $i (@$pname) {
        $ww = &word_html($i);
        $title .= $ww unless $ww eq "\001";
      }
      print "<h2 id=\"$xref\">$title</h2>\n";
    } elsif ($ptype eq "head" || $ptype eq "subh") {
      # Heading or subheading.
      $pflags =~ /.... (.*) :(.*)/;
      $hdr = ($ptype eq "subh" ? "h4" : "h3");
      $title = $1 . " ";
      $xref = $2;
      foreach $i (@$pname) {
        $ww = &word_html($i);
        $title .= $ww unless $ww eq "\001";
      }
      print "<$hdr id=\"$xref\">$title</$hdr>\n";
    } elsif ($ptype eq "code") {
	# Code paragraph.
	$in_code = 1, print "<pre>" unless $in_code;
	print "\n";
	foreach $i (@$pname) {
	    $w = $i;
	    $w =~ s/&/&amp;/g;
	    $w =~ s/</&lt;/g;
	    $w =~ s/>/&gt;/g;
	    print $w, "\n";
	}
    } elsif ($ptype =~ /^(norm|bull|indt|bquo)$/) {
      # Ordinary paragraph, optionally indented.
	if ($ptype eq 'bull') {
	    if (!$in_list) {
		$in_list = 1;
		print "<ul>\n";
	    } else {
		print "</li>\n";
	    }
	    print "<li>\n";
	    $line = '<p>';
	    $endtag = '</p>';
      } elsif ($ptype eq 'indt') {
	  if (!$in_list) {
	      $in_list = 1;
	      print "<ul>\n";
	      print "<li class=\"indt\">\n"; # This is such a hack
	  }
	  $line = '<p>';
	  $endtag = '</p>';
      } elsif ($ptype eq 'bquo') {
	  $in_bquo = 1, print "<blockquote>\n" unless $in_bquo;
	  $line = '<p>';
	  $endtag = '</p>';
      } else {
        $line = '<p>';
        $endtag = '</p>';
      }
      @a = @$pname;
      $wd = $wprev = '';
      do {
        do { $w = &word_html(shift @a) } while $w eq "\001"; # nasty hack
	$wd .= $wprev;
	if ($w eq ' ' || $w eq '' || $w eq undef) {
	  if (length ($line . $wd) > 75) {
	    $line =~ s/\s*$//; # trim trailing spaces
	    print "$line\n";
	    $line = '';
	    $wd =~ s/^\s*//; # trim leading spaces
	  }
	  $line .= $wd;
	  $wd = '';
	}
	$wprev = $w;
      } while ($w ne '' && $w ne undef);
      if ($line =~ /\S/) {
	$line =~ s/\s*$//; # trim trailing spaces
	print $line;
      }
      print $endtag, "\n";
    }
  }

  # Close whichever file was open.
  print "</pre>\n" if ($in_code);
  print "</li>\n</ul>\n" if ($in_list);
  print "</blockquote>\n" if ($in_bquo);
  &html_postamble; select STDOUT; close TEXT;

  print "\n   writing index file...";
  open TEXT, '>', File::Spec->catfile($out_path, 'nasmdoci.html');
  select TEXT;
  &html_preamble(0);
  print "<h2 class=\"index\">Index</h2>\n";
  print "<ul class=\"index\">\n";
  &html_index;
  print "</ul>\n</body>\n</html>\n";
  select STDOUT;
  close TEXT;
}

sub html_preamble {
    print "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n";
    print "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" ";
    print "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n";
    print "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n";
    print "<head>\n";
    print "<title>", $metadata{'title'}, "</title>\n";
    print "<link href=\"nasmdoc.css\" rel=\"stylesheet\" type=\"text/css\" />\n";
    print "<link href=\"local.css\" rel=\"stylesheet\" type=\"text/css\" />\n";
    print "</head>\n";
    print "<body>\n";

    # Navigation bar
    print "<ul class=\"navbar\">\n";
    if (defined($html_nav_last)) {
	my $lastf = $html_fnames{$html_nav_last};
	print "<li class=\"first\"><a class=\"prev\" href=\"$lastf\">$html_nav_last</a></li>\n";
    }
    if (defined($html_nav_next)) {
	my $nextf = $html_fnames{$html_nav_next};
	print "<li><a class=\"next\" href=\"$nextf\">$html_nav_next</a></li>\n";
    }
    print "<li><a class=\"toc\" href=\"nasmdoc0.html\">Contents</a></li>\n";
    print "<li class=\"last\"><a class=\"index\" href=\"nasmdoci.html\">Index</a></li>\n";
    print "</ul>\n";

    print "<div class=\"title\">\n";
    print "<h1>", $metadata{'title'}, "</h1>\n";
    print '<span class="subtitle">', $metadata{'subtitle'}, "</span>\n";
    print "</div>\n";
    print "<div class=\"contents\"\n>\n";
}

sub html_postamble {
    # Common closing tags
    print "</div>\n</body>\n</html>\n";
}

sub html_index {
  my $itag, $a, @ientry, $sep, $w, $wd, $wprev, $line;

  $chapternode = '';
  foreach $itag (@itags) {
    $ientry = $idxmap{$itag};
    @a = @$ientry;
    push @a, "n :";
    $sep = 0;
    foreach $node (@nodes) {
      next if !$idxnodes{$node,$itag};
      push @a, "n ," if $sep;
      push @a, "sp", "x $xrefnodes{$node}", "n $node", "xe$xrefnodes{$node}";
      $sep = 1;
    }
    print "<li class=\"index\">\n";
    $line = '';
    do {
      do { $w = &word_html(shift @a) } while $w eq "\001"; # nasty hack
      $wd .= $wprev;
      if ($w eq ' ' || $w eq '' || $w eq undef) {
        if (length ($line . $wd) > 75) {
	  $line =~ s/\s*$//; # trim trailing spaces
	  print "$line\n";
	  $line = '';
	  $wd =~ s/^\s*//; # trim leading spaces
	}
	$line .= $wd;
	$wd = '';
      }
      $wprev = $w;
    } while ($w ne '' && $w ne undef);
    if ($line =~ /\S/) {
      $line =~ s/\s*$//; # trim trailing spaces
      print $line, "\n";
    }
    print "</li>\n";
  }
}

sub word_html {
  my ($w) = @_;
  my $wtype, $wmajt, $pfx, $sfx;

  return undef if $w eq '' || $w eq undef;

  $wtype = substr($w,0,2);
  $wmajt = substr($wtype,0,1);
  $w = substr($w,2);
  $pfx = $sfx = '';
  $pfx = "<a href=\"$1\">", $sfx = "</a>", $w = $2
    if $wmajt eq "w" && $w =~ /^<(.*)>(.*)$/;
  $w =~ s/&/&amp;/g;
  $w =~ s/</&lt;/g;
  $w =~ s/>/&gt;/g;
  if ($wmajt eq "n" || $wtype eq "e " || $wtype eq "w ") {
    return $pfx . $w . $sfx;
  } elsif ($wtype eq "sp") {
    return ' ';
  } elsif ($wtype eq "da") {
    return '&ndash;';
  } elsif ($wtype eq "dm") {
    return '&mdash;';
  } elsif ($wmajt eq "c" || $wtype eq "wc") {
    return $pfx . "<code>${w}</code>" . $sfx;
  } elsif ($wtype eq "es") {
    return "<em>${w}";
  } elsif ($wtype eq "ee") {
    return "${w}</em>";
  } elsif ($wtype eq "eo") {
    return "<em>${w}</em>";
  } elsif ($wtype eq "x ") {
    # Magic: we must resolve the cross reference into file and marker
    # parts, then dispose of the file part if it's us, and dispose of
    # the marker part if the cross reference describes the top node of
    # another file.
    my $node = $nodexrefs{$w}; # find the node we're aiming at
    my $level = $tstruct_level{$node}; # and its level
    my $up = $node, $uplev = $level-1;
    $up = $tstruct_up{$up} while $uplev--; # get top node of containing file
    my $file = ($up ne $chapternode) ? $html_fnames{$up} : "";
    my $marker = ($level == 1 and $file) ? "" : "#$w";
    return "<a href=\"$file$marker\">";
  } elsif ($wtype eq "xe") {
    return "</a>";
  } elsif ($wmajt eq "i") {
    return "\001";
  } else {
    die "panic in word_html: $wtype$w\n";
  }
}

# Make tree structures. $tstruct_* is top-level and global.
sub add_item {
  my ($item, $level) = @_;
  my $i;

  $tstruct_pname{$item} = $pname;
  $tstruct_next{$tstruct_previtem} = $item;
  $tstruct_prev{$item} = $tstruct_previtem;
  $tstruct_level{$item} = $level;
  $tstruct_up{$item} = $tstruct_last[$level-1];
  $tstruct_mnext{$tstruct_last[$level]} = $item;
  $tstruct_last[$level] = $item;
  for ($i=$level+1; $i<$MAXLEVEL; $i++) { $tstruct_last[$i] = undef; }
  $tstruct_previtem = $item;
  push @nodes, $item;
}

#
# This produces documentation intermediate paragraph format; this is
# basically the digested output of the front end.  Intended for use
# by future backends, instead of putting it all in the same script.
#
sub write_dip {
  open(PARAS, '>', File::Spec->catfile($out_path, 'nasmdoc.dip'));
  foreach $k (sort(keys(%metadata))) {
      print PARAS 'meta :', $k, "\n";
      print PARAS $metadata{$k},"\n";
  }
  for ($para = 0; $para <= $#pnames; $para++) {
      print PARAS $pflags[$para], "\n";
      print PARAS join("\037", @{$pnames[$para]}, "\n");
  }
  foreach $k (@itags) {
      print PARAS 'indx :', $k, "\n";
      print PARAS join("\037", @{$idxmap{$k}}), "\n";
  }
  close(PARAS);
}
