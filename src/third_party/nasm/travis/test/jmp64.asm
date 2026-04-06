	bits 64
	jmp rcx
	jmp [rax]
	jmp qword [rax]
	jmp near [rax]
	jmp near qword [rax]
	jmp far [rax]
	jmp far dword [rax]
	jmp far qword [rax]
	call rcx
	call [rax]
	call qword [rax]
	call near [rax]
	call near qword [rax]
	call far [rax]
	call far dword [rax]
	call far qword [rax]
