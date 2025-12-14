	bits 64

	default rel
	mov dword [foo],12345678h
	mov qword [foo],12345678h
	mov [foo],rax
	mov dword [foo],12345678h
foo:
