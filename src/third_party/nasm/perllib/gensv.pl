#!/usr/bin/perl
#
# Generate a list of rotation vectors so we always use the same set.
# This needs to be run on a platform with /dev/urandom.
#

($n) = @ARGV;

sysopen(UR, '/dev/urandom', O_RDONLY) or die;

$maxlen = 78;

print "\@random_sv_vectors = (\n";
$outl = '   ';

for ($i = 0; $i < $n; $i++) {

    die if (sysread(UR, $x8, 8) != 8);
    @n = unpack("V*", $x8);

    $xl = sprintf(" [0x%08x, 0x%08x]%s",
		  $n[0], $n[1],
		  ($i == $n-1) ? '' : ',');
    if (length($outl.$xl) > $maxlen) {
	print $outl, "\n";
	$outl = '   ';
    }
    $outl .= $xl;
}
close(UR);

print $outl, "\n";
print ");\n";
print "1;\n";
