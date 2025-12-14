;Testname=unoptimized; Arguments=-O0 -fbin -oa32offs.bin; Files=a32offs.bin stdout stderr
;Testname=optimized;   Arguments=-Ox -fbin -oa32offs.bin; Files=a32offs.bin stdout stderr
	bits 16
foo:	a32 loop foo
bar:	loop bar, ecx
	
	bits 32
baz:	a16 loop baz
qux:	loop qux, cx
