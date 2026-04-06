#!/usr/bin/perl
#
# Automatically produce some tables useful for a NASM major mode
#

use integer;
use strict;
use File::Spec;

my($outfile, $srcdir, $objdir) = @ARGV;

if (!defined($outfile)) {
    die "Usage: $0 outfile srcdir objdir\n";
}

$srcdir = File::Spec->curdir() unless (defined($srcdir));
$objdir = $srcdir unless (defined($objdir));

my %tokens = ();

sub xpush($@) {
    my $ref = shift @_;

    $$ref = [] unless (defined($$ref));
    return push(@$$ref, @_);
}

# Combine some specific token types
my %override = ( 'id' => 'special',
		 'float' => 'function',
		 'floatize' => 'function',
		 'strfunc' => 'function',
		 'ifunc' => 'function',
		 'insn' => 'instruction',
		 'reg' => 'register',
		 'seg' => 'special',
		 'wrt' => 'special' );

sub read_tokhash_c($) {
    my($tokhash_c) = @_;

    open(my $th, '<', $tokhash_c)
	or die "$0:$tokhash_c: $!\n";

    my $l;
    my $tokendata = 0;
    while (defined($l = <$th>)) {
	if ($l =~ /\bstruct tokendata tokendata\[/) {
	    $tokendata = 1;
	    next;
	} elsif (!$tokendata) {
	    next;
	}

	last if ($l =~ /\}\;/);

	if ($l =~ /^\s*\{\s*\"(.*?)\",.*?,\s*TOKEN_(\w+),.*\}/) {
	    my $token = $1;
	    my $type  = lc($2);

	    if ($override{$type}) {
		$type = $override{$type};
	    } elsif ($token !~ /^\w/) {
		$type = 'operator';
	    } elsif ($token =~ /^__\?masm_.*\?__$/) {
		next;
	    }
	    xpush(\$tokens{$type}, $token);
	    if ($token =~ /^__\?(.*)\?__$/) {
		# Also encode the "user" (macro) form without __?...?__
		xpush(\$tokens{$type}, $1);
	    }
	}
    }
    close($th);
}

sub read_pptok_c($) {
    my($pptok_c) = @_;

    open(my $pt, '<', $pptok_c)
	or die "$0:$pptok_c: $!\n";

    my $l;
    my $pp_dir = 0;

    while (defined($l = <$pt>)) {
	if ($l =~ /\bpp_directives\[/) {
	    $pp_dir = 1;
	    next;
	} elsif (!$pp_dir) {
	    next;
	}

	last if ($l =~ /\}\;/);

	if ($l =~ /^\s*\"(.*?)\"/) {
	    xpush(\$tokens{'pp-directive'}, $1);
	}
    }
    close($pt);
}

sub read_directiv_dat($) {
    my($directiv_dat) = @_;

    open(my $dd, '<', $directiv_dat)
	or die "$0:$directiv_dat: $!\n";

    my $l;
    my $directiv = 0;

    while (defined($l = <$dd>)) {
	if ($l =~ /^\; ---.*?(pragma)?/) {
	    $directiv = ($1 ne 'pragma');
	    next;
	} elsif (!$directiv) {
	    next;
	}

	if ($l =~ /^\s*(\w+)/) {
	    xpush(\$tokens{'directive'}, $1);
	}
    }

    close($dd);
}

my $version;
sub read_version($) {
    my($vfile) = @_;
    open(my $v, '<', $vfile)
	or die "$0:$vfile: $!\n";

    $version = <$v>;
    chomp $version;

    close($v);
}

sub make_lines($$@) {
    my $maxline = shift @_;
    my $indent  = shift @_;

    # The first line isn't explicitly indented and the last line
    # doesn't end in "\n"; assumed the surrounding formatter wants
    # do control that
    my $linepos   = 0;
    my $linewidth = $maxline - $indent;

    my $line = '';
    my @lines = ();

    foreach my $w (@_) {
	my $l = length($w);

	if ($linepos > 0 && $linepos+$l+1 >= $linewidth) {
	    $line .= "\n" . (' ' x $indent);
	    push(@lines, $line);
	    $linepos = 0;
	    $line = '';
	}
	if ($linepos > 0) {
	    $line .= ' ';
	    $linepos++;
	}
	$line .= $w;
	$linepos += $l;
    }

    if ($linepos > 0) {
	push(@lines, $line);
    }

    return @lines;
}

sub quote_for_emacs(@) {
    return map { s/[\\\"\']/\\$1/g; '"'.$_.'"' } @_;
}

sub write_output($) {
    my($outfile) = @_;

    open(my $out, '>', $outfile)
	or die "$0:$outfile: $!\n";

    my($vol,$dir,$file) = File::Spec->splitpath($outfile);

    print $out ";;; ${file} --- lists of NASM assembler tokens\n";
    print $out ";;;\n";
    print $out ";;; This file contains list of tokens from the NASM x86\n";
    print $out ";;; assembler, automatically extracted from NASM ${version}.\n";
    print $out ";;;\n";
    print $out ";;; This file is intended to be (require)d from a `nasm-mode\'\n";
    print $out ";;; major mode definition.\n";

    foreach my $type (sort keys(%tokens)) {
	print $out "\n(defconst nasm-${type}\n";
	print $out "  \'(";

	print $out make_lines(78, 4, quote_for_emacs(sort @{$tokens{$type}}));
	print $out ")\n";
	print $out "  \"NASM ${version} ${type} tokens for `nasm-mode\'.\")\n";
    }

    close($out);
}

read_tokhash_c(File::Spec->catfile($objdir, 'asm', 'tokhash.c'));
read_pptok_c(File::Spec->catfile($objdir, 'asm', 'pptok.c'));
read_directiv_dat(File::Spec->catfile($srcdir, 'asm', 'directiv.dat'));
read_version(File::Spec->catfile($srcdir, 'version'));

write_output($outfile);
