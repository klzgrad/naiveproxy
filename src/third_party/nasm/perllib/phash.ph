# -*- perl -*-
#
# Perfect Minimal Hash Generator written in Perl, which produces
# C output.
#

require 'random_sv_vectors.ph';
require 'crc64.ph';

#
# Compute the prehash for a key
#
# prehash(key, sv, N)
#
sub prehash($$$) {
    my($key, $n, $sv) = @_;
    my @c = crc64($sv, $key);

    # Create a bipartite graph...
    $k1 = (($c[1] & ($n-1)) << 1) + 0; # low word
    $k2 = (($c[0] & ($n-1)) << 1) + 1; # high word

    return ($k1, $k2);
}

#
# Walk the assignment graph, return true on success
#
sub walk_graph($$$$) {
    my($nodeval,$nodeneigh,$n,$v) = @_;
    my $nx;

    # print STDERR "Vertex $n value $v\n";
    $$nodeval[$n] = $v;

    foreach $nx (@{$$nodeneigh[$n]}) {
	# $nx -> [neigh, hash]
	my ($o, $e) = @$nx;

	# print STDERR "Edge $n,$o value $e: ";
	my $ov;
	if (defined($ov = $$nodeval[$o])) {
	    if ($v+$ov != $e) {
		# Cyclic graph with collision
		# print STDERR "error, should be ", $v+$ov, "\n";
		return 0;
	    } else {
		# print STDERR "ok\n";
	    }
	} else {
	    return 0 unless (walk_graph($nodeval, $nodeneigh, $o, $e-$v));
	}
    }
    return 1;
}

#
# Generate the function assuming a given N.
#
# gen_hash_n(N, sv, \%data, run)
#
sub gen_hash_n($$$$) {
    my($n, $sv, $href, $run) = @_;
    my @keys = keys(%{$href});
    my $i, $sv;
    my $gr;
    my $k, $v;
    my $gsize = 2*$n;
    my @nodeval;
    my @nodeneigh;
    my %edges;

    for ($i = 0; $i < $gsize; $i++) {
	$nodeneigh[$i] = [];
    }

    %edges = ();
    foreach $k (@keys) {
	my ($pf1, $pf2) = prehash($k, $n, $sv);
	($pf1,$pf2) = ($pf2,$pf1) if ($pf1 > $pf2); # Canonicalize order

	my $pf = "$pf1,$pf2";
	my $e = ${$href}{$k};
	my $xkey;

	if (defined($xkey = $edges{$pf})) {
	    next if ($e == ${$href}{$xkey}); # Duplicate hash, safe to ignore
	    if (defined($run)) {
		print STDERR "$run: Collision: $pf: $k with $xkey\n";
	    }
	    return;
	}

	# print STDERR "Edge $pf value $e from $k\n";

	$edges{$pf} = $k;
	push(@{$nodeneigh[$pf1]}, [$pf2, $e]);
	push(@{$nodeneigh[$pf2]}, [$pf1, $e]);
    }

    # Now we need to assign values to each vertex, so that for each
    # edge, the sum of the values for the two vertices give the value
    # for the edge (which is our hash index.)  If we find an impossible
    # sitation, the graph was cyclic.
    @nodeval = (undef) x $gsize;

    for ($i = 0; $i < $gsize; $i++) {
	if (scalar(@{$nodeneigh[$i]})) {
	    # This vertex has neighbors (is used)
	    if (!defined($nodeval[$i])) {
		# First vertex in a cluster
		unless (walk_graph(\@nodeval, \@nodeneigh, $i, 0)) {
		    if (defined($run)) {
			print STDERR "$run: Graph is cyclic\n";
		    }
		    return;
		}
	    }
	}
    }

    # for ($i = 0; $i < $n; $i++) {
    #	print STDERR "Vertex ", $i, ": ", $g[$i], "\n";
    # }

    if (defined($run)) {
	printf STDERR "$run: Done: n = $n, sv = [0x%08x, 0x%08x]\n",
	$$sv[0], $$sv[1];
    }

    return ($n, $sv, \@nodeval);
}

#
# Driver for generating the function
#
# gen_perfect_hash(\%data)
#
sub gen_perfect_hash($) {
    my($href) = @_;
    my @keys = keys(%{$href});
    my @hashinfo;
    my $n, $i, $j, $sv, $maxj;
    my $run = 1;

    # Minimal power of 2 value for N with enough wiggle room.
    # The scaling constant must be larger than 0.5 in order for the
    # algorithm to ever terminate.
    my $room = int(scalar(@keys)*0.8);
    $n = 1;
    while ($n < $room) {
	$n <<= 1;
    }

    # Number of times to try...
    $maxj = scalar @random_sv_vectors;

    for ($i = 0; $i < 4; $i++) {
	printf STDERR "%d vectors, trying n = %d...\n",
		scalar @keys, $n;
	for ($j = 0; $j < $maxj; $j++) {
	    $sv = $random_sv_vectors[$j];
	    @hashinfo = gen_hash_n($n, $sv, $href, $run++);
	    return @hashinfo if (@hashinfo);
	}
	$n <<= 1;
    }

    return;
}

#
# Verify that the hash table is actually correct...
#
sub verify_hash_table($$)
{
    my ($href, $hashinfo) = @_;
    my ($n, $sv, $g) = @{$hashinfo};
    my $k;
    my $err = 0;

    foreach $k (keys(%$href)) {
	my ($pf1, $pf2) = prehash($k, $n, $sv);
	my $g1 = ${$g}[$pf1];
	my $g2 = ${$g}[$pf2];

	if ($g1+$g2 != ${$href}{$k}) {
	    printf STDERR "%s(%d,%d): %d+%d = %d != %d\n",
	    $k, $pf1, $pf2, $g1, $g2, $g1+$g2, ${$href}{$k};
	    $err = 1;
	} else {
	    # printf STDERR "%s: %d+%d = %d ok\n",
	    # $k, $g1, $g2, $g1+$g2;
	}
    }

    die "$0: hash validation error\n" if ($err);
}

1;
