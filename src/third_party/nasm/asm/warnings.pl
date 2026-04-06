#!/usr/bin/perl

use strict;
use Fcntl qw(:seek);
use File::Find;
use File::Basename;

my @warnings = ();
my %aliases  = ();
my %prefixes = ();
my $err = 0;
my $nwarn = 0;

sub quote_for_c(@) {
    my $s = join('', @_);

    $s =~ s/([\"\'\\])/\\$1/g;
    return $s;
}

# Remove a subset of nasmdoc markup
sub remove_markup(@) {
    my $s = join('', @_);

    $s =~ s/\\[\w+](?:\{((?:[^\}]|\\\})*)\})/$1/g;
    $s =~ s/\\(\W)/$1/g;
    return $s;
}

sub add_alias($$) {
    my($a, $this) = @_;
    my @comp = split(/-/, $a);

    $aliases{$a} = $this;

    # All names are prefixes in their own right, although we only
    # list the ones that are either prefixes of "proper names" or
    # the complete alias name.
    for (my $i = ($a eq $this->{name}) ? 0 : $#comp; $i <= $#comp; $i++) {
	my $prefix = join('-', @comp[0..$i]);
	$prefixes{$prefix} = [] unless defined($prefixes{$prefix});
	push(@{$prefixes{$prefix}}, $a);
    }
}

sub find_warnings {
    my $infile = $_;

    return unless (basename($infile) =~ /^\w.*\.[ch]$/i);
    open(my $in, '<', $infile)
	or die "$0: cannot open input file $infile: $!\n";

    my $in_comment = 0;
    my $nline = 0;
    my $this;
    my @doc;

    while (defined(my $l = <$in>)) {
	$nline++;
	chomp $l;

	if (!$in_comment) {
	    $l =~ s/^.*?\/\*.*?\*\///g; # Remove single-line comments

	    if ($l =~ /^.*?(\/\*.*)$/) {
		# Begin block comment
		$l = $1;
		$in_comment = 1;
	    }
	}

	if ($in_comment) {
	    if ($l =~ /\*\//) {
		# End block comment
		$in_comment = 0;
		undef $this;
	    } elsif ($l =~ /^\s*\/?\*\!(\-|\=|\s*)(.*?)\s*$/) {
		my $opr = $1;
		my $str = $2;

		if ($opr eq '' && $str eq '') {
		    next;
		} elsif ((!defined($this) || ($opr eq '')) &&
			 ($str =~ /^([\w\-]+)\s+\[(\w+)\]\s(.*\S)\s*$/)) {
		    my $name = $1;
		    my $def = $2;
		    my $help = $3;

		    my $cname = uc($name);
		    $cname =~ s/[^A-Z0-9_]+/_/g;

		    $this = {name => $name, cname => $cname,
			     def => $def, help => $help,
			     doc => [], file => $infile, line => $nline};

		    if (defined(my $that = $aliases{$name})) {
			# Duplicate definition?!
			printf STDERR "%s:%s: warning %s previously defined at %s:%s\n",
			    $infile, $nline, $name, $that->{file}, $that->{line};
		    } else {
			push(@warnings, $this);
			# Every warning name is also a valid warning alias
			add_alias($name, $this);
			$nwarn++;
		    }
		} elsif ($opr eq '=') {
		    # Alias names for warnings
		    for my $a (split(/,+/, $str)) {
			add_alias($a, $this);
		    }
		} elsif ($opr =~ /^[\-\s]/) {
		    push(@{$this->{doc}}, "$str\n");
		} else {
		    print STDERR "$infile:$nline: malformed warning definition\n";
		    print STDERR "    $l\n";
		    $err++;
		}
	    } else {
		undef $this;
	    }
	}
    }
    close($in);
}

my($what, $outfile, @indirs) = @ARGV;

if (!defined($outfile)) {
    die "$0: usage: [c|h|doc] outfile indir...\n";
}

find({ wanted => \&find_warnings, no_chdir => 1, follow => 1 }, @indirs);

exit(1) if ($err);

my %sort_special = ( 'other' => 1, 'all' => 2 );
sub sort_warnings {
    my $an = $a->{name};
    my $bn = $b->{name};
    return ($sort_special{$an} <=> $sort_special{$bn}) || ($an cmp $bn);
}

@warnings = sort sort_warnings @warnings;
my @warn_noall = @warnings;
pop @warn_noall if ($warn_noall[$#warn_noall]->{name} eq 'all');

my $outdata;
open(my $out, '>', \$outdata)
    or die "$0: cannot create memory file: $!\n";

if ($what eq 'c') {
    print $out "#include \"error.h\"\n\n";
    printf $out "const char * const warning_name[%d] = {\n",
	$#warnings + 2;
    print $out "\tNULL";
    foreach my $warn (@warnings) {
	print $out ",\n\t\"", $warn->{name}, "\"";
    }
    print $out "\n};\n\n";
    printf $out "const struct warning_alias warning_alias[%d] = {",
	scalar(keys %aliases);
    my $sep = '';
    foreach my $alias (sort { $a cmp $b } keys(%aliases)) {
	printf $out "%s\n\t{ %-27s WARN_IDX_%s }",
	    $sep, "\"$alias\",", $aliases{$alias}->{cname};
	$sep = ',';
    }
    print $out "\n};\n\n";

    printf $out "const char * const warning_help[%d] = {\n",
	$#warnings + 2;
    print $out "\tNULL";
    foreach my $warn (@warnings) {
	my $help = quote_for_c(remove_markup($warn->{help}));
	print $out ",\n\t\"", $help, "\"";
    }
    print $out "\n};\n\n";
    printf $out "const uint8_t warning_default[%d] = {\n",
	$#warn_noall + 2;
    print $out "\tWARN_INIT_ON"; # for entry 0
    foreach my $warn (@warn_noall) {
	print $out ",\n\tWARN_INIT_", uc($warn->{def});
    }
    print $out "\n};\n\n";
    printf $out "uint8_t warning_state[%d];\t/* Current state */\n",
	$#warn_noall + 2;
} elsif ($what eq 'h') {
    my $filename = basename($outfile);
    my $guard = $filename;
    $guard =~ s/[^A-Za-z0-9_]+/_/g;
    $guard = "NASM_\U$guard";

    print $out "#ifndef $guard\n";
    print $out "#define $guard\n";
    print $out "\n";
    print $out "#ifndef WARN_SHR\n";
    print $out "# error \"$filename should only be included from within error.h\"\n";
    print $out "#endif\n\n";
    print $out "enum warn_index {\n";
    printf $out "\tWARN_IDX_%-23s = %3d, /* not suppressible */\n", 'NONE', 0;
    my $n = 1;
    foreach my $warn (@warnings) {
	printf $out "\tWARN_IDX_%-23s = %3d%s /* %s */\n",
	    $warn->{cname}, $n,
	    ($n == $#warnings + 1) ? " " : ",",
	    $warn->{help};
	$n++;
    }
    print $out "};\n\n";

    print $out "enum warn_const {\n";
    printf $out "\tWARN_%-27s = %3d << WARN_SHR", 'NONE', 0;
    $n = 1;
    foreach my $warn (@warn_noall) {
	printf $out ",\n\tWARN_%-27s = %3d << WARN_SHR", $warn->{cname}, $n++;
    }
    print $out "\n};\n\n";

    print $out "struct warning_alias {\n";
    print $out "\tconst char *name;\n";
    print $out "\tenum warn_index warning;\n";
    print $out "};\n\n";
    printf $out "#define NUM_WARNING_ALIAS %d\n", scalar(keys %aliases);

    printf $out "extern const char * const warning_name[%d];\n",
	$#warnings + 2;
    printf $out "extern const char * const warning_help[%d];\n",
	$#warnings + 2;
    print $out "extern const struct warning_alias warning_alias[NUM_WARNING_ALIAS];\n";
    printf $out "extern const uint8_t warning_default[%d];\n",
	$#warn_noall + 2;
    printf $out "extern uint8_t warning_state[%d];\n",
	$#warn_noall + 2;
    print $out "\n#endif /* $guard */\n";
} elsif ($what eq 'doc') {
    my %whatdef = ( 'on' => 'Enabled',
		    'off' => 'Disabled',
		    'err' => 'Enabled and promoted to error' );

    my @indexinfo = ();
    my @outtxt    = ();

    foreach my $pfx (sort { $a cmp $b } keys(%prefixes)) {
	my $warn = $aliases{$pfx};
	my @doc;

	if (!defined($warn)) {
	    my @plist = sort { $a cmp $b } @{$prefixes{$pfx}};
	    next if ( $#plist < 1 );

	    @doc = ("all \\c{$pfx-} warnings\n\n",
		    "\\> \\c{$pfx} is a group alias for all warning classes\n",
		    "prefixed by \\c{$pfx-}; currently\n");
	    # Just commas is bad grammar to be sure, but it is more
	    # legible than the alternative.
	    push(@doc, join(scalar(@plist) < 3 ? ' and ' : ', ',
			    map { "\\c{$_}" } @plist).".\n");
	} elsif ($pfx ne $warn->{name}) {
	    my $awarn = $aliases{$warn->{name}};
	    @doc = ($awarn->{help}."\n\n",
		    "\\> \\c{$pfx} is a backwards compatibility alias for \\c{".
		    $warn->{name}."}.\n");
	} else {
	    my $docdef = $whatdef{$warn->{def}};

	    @doc = ($warn->{help}."\n\n",
		    "\\> \\c{".$warn->{name}."} ");

	    my $newpara = 0;
	    foreach my $l (@{$warn->{doc}}) {
		if ($l =~ /^\s*$/) {
		    $newpara = 1;
		} else {
		    if ($newpara && $l !~ /^\\c\s+/) {
			$l = '\> ' . $l;
		    }
		    $newpara = 0;
		}
		push(@doc, $l);
	    }
	    if (defined($docdef)) {
		push(@doc, "\n", "\\> $docdef by default.\n");
	    }
	}

	push(@indexinfo, "\\IR{w-$pfx} warning class, \\c{$pfx}\n");
	push(@outtxt, "\\b \\I{w-$pfx} \\c{$pfx}: ", @doc, "\n");
    }

    print $out "\n", @indexinfo, "\n", @outtxt;
}

close($out);

# Write data to file if and only if it has changed
# For some systems, even if we don't write, opening for append
# apparently touches the timestamp, so we need to read and write
# as separate operations.
if (open(my $out, '<', $outfile)) {
    my $datalen = length($outdata);
    my $oldlen = read($out, my $oldoutdata, $datalen+1);
    close($out);
    exit 0 if (defined($oldlen) && $oldlen == $datalen &&
	       ($oldoutdata eq $outdata));
}

# Data changed, must rewrite
open(my $out, '>', $outfile)
    or die "$0: cannot open output file $outfile: $!\n";

print $out $outdata;
close($out);
