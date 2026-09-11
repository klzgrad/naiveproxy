#!/usr/bin/perl

use Font::TTF::Font;
use Font::TTF::Head;
use Font::TTF::Hmtx;
use Font::TTF::Cmap;
use Font::TTF::Maxp;
use Font::TTF::PSNames;
use Font::TTF::Post;

use strict;

sub parse_ttf_file($) {
    my($filename) = @_;

    my $fontdata = {
	widths => {},
	kern => {}
    };

    my $f = Font::TTF::Font->open($filename);

    return undef if (!defined($f));

    $fontdata->{file} = $filename;
    $fontdata->{type} = defined($f->{' CFF'}) ? 'otf' : 'ttf';

    $f->{head}->read();
    $fontdata->{scale} = $f->{head}{unitsPerEm};

    $f->{maxp}->read();
    my $glyphs = $f->{maxp}{numGlyphs};

    $f->{cmap}->read();
    $f->{hmtx}->read();
    $f->{name}->read();
    $fontdata->{name} = $f->{name}->find_name(6); # PostScript name
    $f->{post}->read();
    my $psglyphs = 0;
    my $psmap = $f->{post}->{VAL};
    $psmap = [] if (!defined($psmap));
    #printf "Glyphs with PostScript names: %d\n", scalar(@$psmap);

    # Can be done as an array of arrays in case of multiple unicodes to
    # one glyph...
    my @unimap = $f->{cmap}->reverse();

    for (my $i = 0; $i < $glyphs; $i++) {
	my $width = $f->{hmtx}->{advance}[$i];
	my $psname = $psmap->[$i];
	if (!defined($psname)) {
	    $psname = Font::TTF::PSNames::lookup($unimap[$i]);
	}
	next if (!defined($psname) || ($psname eq '.notdef'));
	$fontdata->{widths}{$psname} = $f->{hmtx}->{advance}[$i];
    }

    $f->release;

    return $fontdata;
}

1;
