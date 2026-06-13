#!/usr/bin/perl

print ";Testname=unoptimized; Arguments=-fbin -oriprel.bin -O0; Files=stdout stderr riprel.bin\n";
print ";Testname=optimized;   Arguments=-fbin -oriprel.bin -Ox; Files=stdout stderr riprel.bin\n";


print "\tbits 64\n";

foreach $mode ('abs', 'rel') {
    print "\n\tdefault $mode\n\n";

    foreach $so ('', 'fs:', 'es:') {
	foreach $rq ('', 'abs ', 'rel ') {
	    foreach $ao ('', 'a64 ', 'a32 ') {
		foreach $sq ('', 'dword ', 'qword ') {
		    foreach $v ('foo', '0xaaaaaaaaaaaaaaaa', '0xbbbbbbbb',
				'0xffffffffcccccccc') {
			foreach $r (	'al', 'bl', 'ax', 'bx', 'eax', 'ebx', 'rax', 'rbx') {
			    print "\tmov $r,[$ao$rq$sq$so$v]\n";
			}
		    }
		    print "\n";
		}
	    }
	}
    }
}

print "\nfoo:\n";
