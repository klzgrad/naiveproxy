#!/usr/bin/perl
#
# Generate a test case for label lookup performance
#

($len) = @ARGV;
$len = 100000 unless ($len);

print "\tbits 32\n";
print "\tsection .data\n";
print "\n";

for ($i = 0; $i < $len; $i++) {
    print "l$i:\n";
    for ($j = 0; $j < 8; $j++) {
	print "\tdd l", int(rand($i+1)), "\n";
    }
}
