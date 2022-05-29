;Testname=unoptimized; Arguments=-fbin -onop.bin;     Files=stdout stderr nop.bin
;Testname=optimized;   Arguments=-fbin -onop.bin -Ox; Files=stdout stderr nop.bin

	bits 64

	nop
	o64 nop
	pause
	o64 pause

	xchg ax,ax
	xchg eax,eax
	xchg rax,rax
	
	rep xchg ax,ax
	rep xchg eax,eax
	rep xchg rax,rax
