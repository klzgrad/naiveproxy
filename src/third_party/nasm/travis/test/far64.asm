; BR 2039212
	bits 64

	call qword far [rax]
	jmp qword far [rax]
	call dword far [rax]
	jmp dword far [rax]
	call far [rax]
	jmp far [rax]
