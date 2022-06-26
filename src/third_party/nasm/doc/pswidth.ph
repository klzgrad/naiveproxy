#!/usr/bin/perl
#
# Get the width of a PostScript string in PostScript points (1/72")
# given a set of font metrics and an encoding vector.
#
sub ps_width($$$) {
    my($str, $met, $encoding) = @_;
    my($w) = 0;
    my($i,$c,$p);

    $l = length($str);
    undef $p;
    for ( $i = 0 ; $i < $l ; $i++ ) {
	$c = substr($str,$i,1);
	$w += $$met{widths}{$encoding->[ord($c)]};
	# The standard PostScript "show" operator doesn't do kerning.
	# $w += $$met{kern}{$p.$c};
	$p = $c;
    }
    
    return $w / $met->{scale};
}

# OK
1;
