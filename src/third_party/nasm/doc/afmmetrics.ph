#!/usr/bin/perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright 1996-2017 The NASM Authors - All Rights Reserved

#
# Parse AFM metric file, returns a reference to fontdata
#
sub parse_afm_file($$) {
    my($filename, $filetype) = @_;

    my $fontdata = {
	widths => {},
	kern => {}
    };

    my $charmetrics = 0;
    my $kerndata = 0;
    my $charcode, $width, $name;

    my $fontfile = $filename.'.'.$filetype;
    return undef unless ( -f $fontfile );

    $fontdata->{file} = $fontfile;
    $fontdata->{type} = $filetype;
    $fontdata->{scale} = 1000;	# AFM metrics always have scale 1000

    return undef unless (open(my $fh, '<', $filename.'.afm'));

    while ( my $line = <$fh> ) {
	if ( $line =~ /^\s*FontName\s+(.*)\s*$/i ) {
	    $fontdata->{'name'} = $1;
	} elsif ( $line =~ /^\s*StartCharMetrics\b/i ) {
	    $charmetrics = 1;
	} elsif ( $line =~ /^\s*EndCharMetrics\b/i ) {
	    $charmetrics = 0;
	} elsif ( $line =~ /^\s*StartKernPairs\b/i ) {
	    $kerndata = 1;
	} elsif ( $line =~ /^\s*EndKernPairs\b/i ) {
	    $kerndata = 0;
	} elsif ( $charmetrics ) {
	    my @data = split(/\s*;\s*/, $line);
	    undef $charcode, $width, $name;
	    foreach my $d ( @data ) {
		my @dd = split(/\s+/, $d);
		if ( $dd[0] eq 'C' ) {
		    $charcode = $dd[1];
		} elsif ( $dd[0] eq 'WX' ) {
		    $width = $dd[1];
		} elsif ( $dd[0] eq 'W' ) {
		    $width = $dd[2];
		} elsif ( $dd[0] eq 'N' ) {
		    $name = $dd[1];
		}
	    }
	    if ( defined($name) && defined($width) ) {
		$fontdata->{widths}{$name} = $width;
	    }
	} elsif ( $kerndata ) {
	    my($kpx, $a, $b, $adj) = split(/\s+/, $line);
	    if ( $kpx eq 'KPX' ) {
		if (!exists($fontdata->{kern}{$a})) {
		    $fontdata->{kern}{$a} = {};
		}
		$fontdata->{kern}{$a}{$b} = $adj;
	    }
	}
    }

    return $fontdata;
}

1;
