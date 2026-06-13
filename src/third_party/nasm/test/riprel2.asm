;Testname=unoptimized; Arguments=-fbin -oriprel2.bin -O0; Files=stdout stderr riprel.bin
;Testname=optimized;   Arguments=-fbin -oriprel2.bin -Ox; Files=stdout stderr riprel.bin

	bits 64

	default rel, fs:abs, gs:rel
	%note default __?DEFAULT?__
	%note %findi(fs:abs, %[__?DEFAULT?__])
	%note %findi(gs:abs, %[__?DEFAULT?__])
	%note %findi(abs, %[__?DEFAULT?__])
	%note %findi(fs:abs,fs:abs)
	%note %find(a)
	%note %find(a,b)
	%note %find(a,b,c)
	%note %find(a,b,a,c)
	%note %find(a,b,a,%error("Not found"))

	mov dword [foo],12345678h
	mov qword [foo],12345678h
	mov [foo],rax

	mov dword [es:foo],12345678h
	mov qword [es:foo],12345678h
	mov [es:foo],rax

	mov dword [fs:foo],12345678h
	mov qword [fs:foo],12345678h
	mov [fs:foo],rax

	mov dword [gs:foo],12345678h
	mov qword [gs:foo],12345678h
	mov [gs:foo],rax
foo:
