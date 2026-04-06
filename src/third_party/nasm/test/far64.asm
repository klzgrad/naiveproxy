;Testname=test; Arguments=-fbin -ofar64.bin; Files=stdout stderr far64.bin
; BR 2039212
	bits 64

	call qword far [rax]
	jmp qword far [rax]
	call dword far [rax]
	jmp dword far [rax]
	call far [rax]
	jmp far [rax]
