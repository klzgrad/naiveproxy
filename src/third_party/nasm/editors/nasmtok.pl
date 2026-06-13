#!/usr/bin/perl
#
# Automatically produce some tables useful for a NASM major mode
#

use integer;
use strict;
use File::Spec;
use File::Find;

my $format = 'el';

if ($ARGV[0] =~ /^-(\S+)$/) {
    $format = $1;
    shift @ARGV;
}

my($outfile, $srcdir, $objdir) = @ARGV;

if (!defined($outfile)) {
    die "Usage: $0 [-format] outfile srcdir objdir\n";
}

my @vpath;

$srcdir = $srcdir || File::Spec->curdir();
$objdir = $objdir || $srcdir;
push(@vpath, $objdir) if ($objdir ne $srcdir);
push(@vpath, $srcdir);

my %tokens = ();		# Token lists per category
my %token_category = ();	# Tokens to category map

sub xpush($@) {
    my $ref = shift @_;

    $$ref = [] unless (defined($$ref));
    return push(@$$ref, @_);
}

# Search for a file, and return a file handle if successfully opened
sub open_vpath($$) {
    my($mode, $file) = @_;
    my %tried;

    # For simplicity, allow filenames to be specified
    # with Unix / syntax internally
    $file = File::Spec->catfile(split(/\//, $file));

    foreach my $d (@vpath) {
	my $fn = File::Spec->catfile($d, $file);
	next if ($tried{$fn});
	$tried{$fn}++;
	my $fh;
	return $fh if (open($fh, $mode, $fn));
    }
    return undef;
}

sub must_open($) {
    my($file) = @_;
    my $fh = open_vpath('<', $file);
    return $fh if (defined($fh));
    die "$0:$file: $!\n";
}

# Combine some specific token types
my %override = (
    'brcconst' => 'special-constant',
    'id' => 'special',
    'float' => 'function',
    'floatize' => 'function',
    'strfunc' => 'function',
    'ifunc' => 'function',
    'insn' => 'instruction',
    'reg' => 'register',
    'seg' => 'special',
    'wrt' => 'special',
    'times' => 'special');

sub addtoken($@) {
    my $type = shift @_;

    foreach my $token (@_) {
	unless (defined($token_category{$token})) {
	    $type = $override{$type} if (defined($override{$type}));
	    xpush(\$tokens{$type}, $token);
	    $token_category{$token} = $type;
	}
    }
}

sub read_tokhash_c($) {
    my($tokhash_c) = @_;

    my $th = must_open($tokhash_c);

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

	if ($l =~ /^\s*\{\s*\"(.*?)\",.*?,\s*TOKEN_(\w+),(.*)\}/) {
	    my $token = $1;
	    my $type  = lc($2);
	    my $flags = $3;

	    $token = "{${token}}" if ($flags =~ /\bTFLAG_BRC\b/);

	    # Parametric token: omit the actual parameter(s)
	    $token =~ s/^(\{[\w-]+=).+(\})$/$1$2/;

	    if ($token !~ /^(\{[\w-]+=?\}|\w+)$/) {
		$type = 'operator';
	    } elsif ($token =~ /^__\?masm_.*\?__$/) {
		next;
	    }
	    addtoken($type, $token);
	    if ($token =~ /^__\?(.*)\?__$/) {
		# Also encode the "user" (macro) form without __?...?__
		addtoken($type, $1);
	    }
	}
    }
    close($th);
}

sub read_pptok_c($) {
    my($pptok_c) = @_;

    my $pt = must_open($pptok_c);

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
	    addtoken('pp-directive', $1);
	}
    }
    close($pt);
}

sub read_directiv_dat($) {
    my($directiv_dat) = @_;

    my $dd = must_open($directiv_dat);

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
	    addtoken('directive', $1);
	}
    }

    close($dd);
}

my %version;
sub read_version($) {
    my($vfile) = @_;
    my $v = must_open($vfile);

    while (defined(my $vl = <$v>)) {
	if ($vl =~ /^NASM_(\w+)=(\S+)\s*$/) {
	    $version{lc($1)} = $2;
	}
    }
    close($v);
}

# This is called from the directory search in read_macros(), so
# don't use must_open() here.
sub read_macro_file($) {
    my($file) = @_;

    open(my $fh, '<', $file) or die "$0:$file: $!\n";
    while (defined(my $l = <$fh>)) {
	next unless ($l =~ /^\s*\%/);
	my @f = split(/\s+/, $l);
	next unless (scalar(@f) >= 2);
	next if ($f[1] =~ /^[\%\$][^\(]+$/); # Internal use only
	$f[1] =~ s/\(.*$//;	# Strip argument list if any
	$f[1] = lc($f[1]) if ($f[0] =~ /^\%i/);
	if ($f[0] =~ /^\%(i)?(assign|defalias|define|defstr|substr|xdefine)\b/) {
	    addtoken('smacro', $f[1]);
	} elsif ($f[0] =~ /^\%i?macro$/) {
	    addtoken('mmacro', $f[1]);
	}
    }
    close($fh);
}

sub read_macros(@) {
    my %visited;
    my @dirs = (File::Spec->curdir(), qw(macros output editors));
    @dirs = map { my $od = $_; map { File::Spec->catdir($od, $_) } @dirs } @_;
    foreach my $dir (@dirs) {
	next if ($visited{$dir});
	$visited{$dir}++;
	next unless opendir(my $dh, $dir);
	while (defined(my $fn = readdir($dh))) {
	    next unless ($fn =~ /\.mac$/i);
	    read_macro_file(File::Spec->catfile($dir, $fn));
	}
	closedir($dh);
    }
}

# Handle special tokens which may not have been picked up by the automatic
# process, because they depend on the build parameters, or are buried
# deep in C code...
sub add_special_cases() {
    # Not defined in non-snapshot builds
    addtoken('smacro', '__NASM_SNAPSHOT__', '__?NASM_SNAPSHOT?__');
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

# Emacs LISP
sub write_output_el {
    my($out, $outfile, $file) = @_;
    my $whoami = 'NASM '.$version{'ver'};

    print $out ";;; ${file} --- lists of NASM assembler tokens\n\n";
    print $out ";;; Commentary:\n\n";
    print $out ";; This file contains list of tokens from the NASM x86\n";
    print $out ";; assembler, automatically extracted from ${whoami}.\n";
    print $out ";;\n";
    print $out ";; This file is intended to be (require)d from a `nasm-mode\'\n";
    print $out ";; major mode definition.\n";
    print $out ";;\n";
    print $out ";; Tokens that are only recognized inside curly braces are\n";
    print $out ";; noted as such. Tokens of the form {xxx=} are parametric\n";
    print $out ";; tokens, where the token may contain additional text on\n";
    print $out ";; the right side of the = sign. For example,\n";
    print $out ";; {dfv=} should be matched by {dfv=cf,zf}.\n";
    print $out "\n";
    print $out ";;; Code:\n";

    my @types = sort keys(%tokens);

    # Write the individual token type lists
    foreach my $type (sort keys(%tokens)) {
	print $out "\n(defconst nasm-${type}\n";
	print $out "  \'(";

	print $out make_lines(78, 4, quote_for_emacs(sort @{$tokens{$type}}));
	print $out ")\n";
	print $out "  \"${whoami} ${type} tokens for `nasm-mode\'.\")\n";
    }

    # Generate a list of all the token type lists.
    print $out "\n(defconst nasm-token-lists\n";
    print $out "  \'(";
    print $out make_lines(78, 4, map { "'nasm-$_" } sort keys(%tokens));
    print $out ")\n";
    print $out "  \"List of all ${whoami} token type lists.\")\n";

    # The NASM token extracted version
    printf $out "\n(defconst nasm-token-version %s\n",
	quote_for_emacs($version{'ver'});
    print $out "  \"Version of NASM from which tokens were extracted,\n";
    print $out "as a human-readable string.\")\n";

    printf $out "\n(defconst nasm-token-version-id #x%08x\n",
	$version{'version_id'};
    print $out "  \"Version of NASM from which tokens were extracted,\n";
    print $out "as numeric identifier, for comparisons. Equivalent to the\n";
    print $out "__?NASM_VERSION_ID?__ NASM macro value.\")\n";

    printf $out "\n(defconst nasm-token-version-snapshot %s\n",
	$version{'snapshot'} || 'nil';
    print $out "  \"Daily NASM snapshot build from which tokens were extracted,\n";
    print $out "as a decimal number in YYYYMMDD format, or nil if not a\n";
    print $out "daily snapshot build.\")\n";

    # Footer
    print $out "\n(provide 'nasmtok)\n";
    print $out ";;; ${file} ends here\n";

    return 0;
}

# JSON
sub write_output_json {
    use JSON;

    my($out, $outfile, $file) = @_;
    my $whoami = 'NASM '.$version{'ver'};


    my $json = JSON->new;
    $json = $json->ascii(1)->canonical(1);

    my %ver;
    foreach my $vn (keys(%version)) {
	my $vv = $version{$vn};
	next if ($vn eq 'version_xid');
	$vn =~ s/_ver$//;
	$vn =~ s/^version_//;
	$vv = $vv + 0 if ($vn ne 'ver');
	$ver{$vn} = $vv;
    }

    print $out $json->encode({
	'$comment' => "NASM syntax information extracted from ${whoami}",
	    'tokens' => \%tokens, 'version' => \%ver});
    print $out "\n";
    return 0;
}

sub write_output($$) {
    my($format, $outfile) = @_;
    my %formats = (
	'el' => \&write_output_el,
	'json' => \&write_output_json
    );

    my $outfunc = $formats{$format};
    if (!defined($outfunc)) {
	die "$0: unknown output format: $format\n";
    }

    open(my $out, '>', $outfile)
	or die "$0:$outfile: $!\n";

    my($vol,$dir,$file) = File::Spec->splitpath($outfile);

    my $err = $outfunc->($out, $outfile, $file);
    close($out);

    if ($err) {
	unlink($outfile);
	die "$0:$outfile: error writing output\n";
    }
}

add_special_cases();
read_tokhash_c('asm/tokhash.c');
read_pptok_c('asm/pptok.c');
read_directiv_dat('asm/directiv.dat');
read_version('version.mak');
read_macros(@vpath);
write_output($format, $outfile);
