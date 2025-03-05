#!/usr/bin/perl
#
# Re-align the columns in insns.dat, and enforce case conventions
#

@cols = (0, 16, 48, 96);

while ($line = <STDIN>) {
    chomp $line;
    if ($line !~ /^\s*(\;.*|)$/) {
	($ln = $line) =~ s/\s+$//;
	if ($line =~ /^\s*(\S+)\s+(\S+)\s+(\S+|\[.*\])\s+(\S+)\s*$/) {
	    @fields = ($1, $2, $3, $4);
	    $fields[0] = "\U$fields[0]" unless ($fields[0] =~ /^[^a-z]+cc$/);
	    $fields[3] =~ s/\,+$//;
	    $fields[3] = "\U$fields[3]" unless ($fields[3] eq 'ignore');
	    $c = 0;
	    $line = '';
	    for ($i = 0; $i < scalar(@fields); $i++) {
		if ($i > 0 && $c >= $cols[$i]) {
		    $line .= ' ';
		    $c++;
		}	
		while ($c < $cols[$i]) {
		    $line .= "\t";
		    $c = ($c+8)	& ~7;
		}
		$line .= $fields[$i];
		for ($j = 0; $j < length($fields[$i]); $j++) {
		    if (substr($fields[$i], $j, 1) eq "\t") {
			$c = ($c+8) & ~7;
		    } else {
			$c++;
		    }
		}
	    }
	}
    }
    print $line, "\n";
}
