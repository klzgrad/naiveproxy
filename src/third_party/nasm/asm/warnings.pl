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

sub read_warnings($) {
    my($infile) = @_;

    open(my $in, '<', $infile) or die "$0:$infile: $!\n";

    my $nline = 0;
    my $this;
    my @doc;

    while (defined(my $l = <$in>)) {
	$nline++;
	$l =~ s/\s+$//;
	if ($l ne '') {
	    $l =~ s/^\s*\#(\s.*)?$//;
	    $l =~ s/\s+\\\#(\s.*)?$//;
	    next if ($l eq '');
	}

	if ($l =~ /^([\w\-]+)\s+\[(\w+)\]\s+(.*)$/) {
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
	} elsif ($l =~ /^\=([\w\-,]+)$/) {
	    # Alias names for warnings
	    die unless (defined($this));
	    map { add_alias($_,$this) } split(/,+/, $1);
	} elsif ($l =~ /^(\s+(.*))?$/) {
	    my $str = $2;
	    die unless (defined($this));
	    next if ($str eq '' && !scalar(@{$this->{doc}}));
	    push(@{$this->{doc}}, "$str\n");
	} else {
	    print STDERR "$infile:$nline: malformed warning definition\n";
	    print STDERR "    $l\n";
	    $err++;
	}
    }
    close($in);
}

my($what, $outfile, @infiles) = @ARGV;

if (!defined($outfile)) {
    die "$0: usage: [c|h|doc] outfile infiles...\n";
}

foreach my $file (@infiles) {
    read_warnings($file);
}

exit(1) if ($err);

my %sort_special = ( 'other' => 1, 'all' => 2 );
sub sort_warnings {
    my $an = $a->{name};
    my $bn = $b->{name};
    return ($sort_special{$an} <=> $sort_special{$bn}) || ($an cmp $bn);
}

@warnings = sort sort_warnings @warnings;
my @warn_noall = grep { !($_->{name} eq 'all') } @warnings;

my $outdata;
open(my $out, '>', \$outdata)
    or die "$0: cannot create memory file: $!\n";

if ($what eq 'c') {
    print $out "#include \"error.h\"\n\n";
    printf $out "const char * const warning_name[%d] = {\n",
	$#warnings + 2;
    print $out "    NULL";
    foreach my $warn (@warnings) {
	print $out ",\n    \"", $warn->{name}, "\"";
    }
    print $out "\n};\n\n";
    printf $out "const struct warning_alias warning_alias[%d] = {",
	scalar(keys %aliases);
    my $sep = '';
    foreach my $alias (sort { $a cmp $b } keys(%aliases)) {
	printf $out "%s\n    { %-39s WARN_IDX_%-31s }",
	    $sep, "\"$alias\",", $aliases{$alias}->{cname};
	$sep = ',';
    }
    print $out "\n};\n\n";

    printf $out "const char * const warning_help[%d] = {\n",
	$#warnings + 2;
    print $out "    NULL";
    foreach my $warn (@warnings) {
	my $help = quote_for_c(remove_markup($warn->{help}));
	print $out ",\n    \"", $help, "\"";
    }
    print $out "\n};\n\n";
    printf $out "const uint8_t warning_default[%d] = {\n",
	$#warn_noall + 2;
    print $out "    WARN_INIT_ON"; # for entry 0
    foreach my $warn (@warn_noall) {
	print $out ",\n    WARN_INIT_", uc($warn->{def});
    }
    print $out "\n};\n\n";
    printf $out "uint8_t warning_state[%d];    /* Current state */\n",
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
    printf $out "    WARN_IDX_%-31s = %3d, /* not suppressible */\n", 'NONE', 0;
    my $n = 1;
    foreach my $warn (@warnings) {
	printf $out "    WARN_IDX_%-31s = %3d%s /* %s */\n",
	    $warn->{cname}, $n,
	    ($n == $#warnings + 1) ? " " : ",",
	    remove_markup($warn->{help});
	$n++;
    }
    print $out "};\n\n";

    print $out "enum warn_const {\n";
    printf $out "    WARN_%-35s = %3d << WARN_SHR", 'NONE', 0;
    $n = 1;
    foreach my $warn (@warn_noall) {
	printf $out ",\n    WARN_%-35s = %3d << WARN_SHR", $warn->{cname}, $n++;
    }
    print $out "\n};\n\n";

    print $out "struct warning_alias {\n";
    print $out "    const char *name;\n";
    print $out "    enum warn_index warning;\n";
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
    my %wsec = ('on' => [], 'off' => [], 'err' => [],
		'group' => [], 'legacy' => []);

    my @indexinfo = ();

    foreach my $pfx (sort { $a cmp $b } keys(%prefixes)) {
	my $warn = $aliases{$pfx};
	my @doc;
	my $wtxt;

	if (!defined($warn)) {
	    my @plist = sort { $a cmp $b } @{$prefixes{$pfx}};
	    next if ( $#plist < 1 );

	    @doc = ("group alias for:\n\n");
	    push(@doc, map { "\\c      $_\n" } @plist);
	    $wtxt = $wsec{'group'};
	} elsif ($pfx ne $warn->{name}) {
	    my $awarn = $aliases{$warn->{name}};
	    @doc = ($awarn->{help}."\n\n",
		    "\\> Alias for \\c{".$warn->{name}."}.\n");
	    $wtxt = $wsec{'legacy'};
	} else {
	    @doc = ($warn->{help}."\n\n");

	    my $newpara = 1;
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

	    $wtxt = $wsec{$warn->{def}};
	}

	push(@indexinfo, "\\IR{w-$pfx} warning class, \\c{$pfx}\n");
	push(@$wtxt, "\\b \\I{w-$pfx} \\c{$pfx}: ", @doc, "\n");
    }

    print $out "\n", @indexinfo, "\n";
    print $out "\n\\H{warning-classes} Warning Classes\n\n";
    print $out "This list shows each warning class that can be\n";
    print $out "enabled or disabled individually. Each warning containing\n";
    print $out "a \\c{-} character in the name can also be enabled or\n";
    print $out "disabled as part of a group, named by removing one or more\n";
    print $out "\\c{-}-delimited suffixes.\n";

    print $out "\n\\S{warnings-classes-on} Enabled by default\n\n";
    print $out @{$wsec{'on'}};

    print $out "\n\\S{warnings-classes-err} Enabled and promoted to error by default\n\n";
    print $out @{$wsec{'err'}};

    print $out "\n\\S{warnings-classes-off} Disabled by default\n\n";
    print $out @{$wsec{'off'}};

    print $out "\n\\H{warning-groups} Warning Class Groups\n\n";
    print $out "Warning class groups are aliases for all warning classes with a common\n";
    print $out "prefix. This list shows the warnings that are currently\n";
    print $out "included in specific warning groups.\n\n";
    print $out @{$wsec{'group'}};

    print $out "\n\\H{warning-legacy} Warning Class Aliases for Backward Compatiblity\n\n";
    print $out "These aliases are defined for compatibility with earlier\n";
    print $out "versions of NASM.\n\n";
    print $out @{$wsec{'legacy'}};
}

close($out);

open(my $out, '>', $outfile)
    or die "$0: cannot open output file $outfile: $!\n";

print $out $outdata;
close($out);
