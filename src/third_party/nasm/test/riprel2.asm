;Testname=unoptimized; Arguments=-fbin -oriprel2.bin -O0; Files=stdout stderr riprel.bin
;Testname=optimized;   Arguments=-fbin -oriprel2.bin -Ox; Files=stdout stderr riprel.bin

	bits 64

	default rel
	mov dword [foo],12345678h
	mov qword [foo],12345678h
	mov [foo],rax
	mov dword [foo],12345678h
foo:
