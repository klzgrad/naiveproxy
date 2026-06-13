#!/usr/bin/perl

@list = ('', 'ZMACRO', 'NMACRO', 'TMACRO', '1', '+1', '1 2', '1,2',
	 'foo', 'foo bar', '%', '+foo', '<<');
@tests = ('token', 'empty');

print ";Testname=test; Arguments=-fbin -oiftoken.txt; Files=stdout stderr iftoken.txt"
print "%define ZMACRO\n";
print "%define NMACRO 1\n";
print "%define TMACRO 1 2\n";

foreach $x (@list) {
    print "\tdb 'N \"$x\":'\n";
    foreach $t (@tests) {
	print "%if$t $x\n";
	print "\tdb ' $t'\n";
	print "%else\n";
	print "\tdb ' n$t'\n";
	print "%endif\n";
    }
    print "\tdb 10\n";

    print "\tdb 'C \"$x\":'\n";
    foreach $t (@tests) {
	print "%if$t $x ; With a comment!\n";
	print "\tdb ' $t'\n";
	print "%else\n";
	print "\tdb ' n$t'\n";
	print "%endif\n";
    }
    print "\tdb 10\n";
}
