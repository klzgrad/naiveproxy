;Testname=imm64-O0; Arguments=-O0 -fbin -oimm64.bin; Files=stdout stderr imm64.bin
;Testname=imm64-O1; Arguments=-O1 -fbin -oimm64.bin; Files=stdout stderr imm64.bin
;Testname=imm64-Ox; Arguments=-Ox -fbin -oimm64.bin; Files=stdout stderr imm64.bin

	bits 64
	mov rax,11223344h
	mov rax,dword 11223344h
	mov eax,11223344h
	mov [rax],dword 11223344h		; 32-bit operation
	mov qword [rax],11223344h
	mov qword [rax],dword 11223344h

	mov rax,0_ffffffff_8899aabbh
	mov rax,dword 0_ffffffff_8899aabbh
	mov eax,0_ffffffff_8899aabbh
	mov [rax],dword 0_ffffffff_8899aabbh	; 32-bit operation
	mov qword [rax],0_ffffffff_8899aabbh
	mov qword [rax],dword 0_ffffffff_8899aabbh

	mov rax,7fffffffh
	mov rax,80000000h
	mov rax,0_ffffffffh
	mov rax,1_00000000h
	mov rax,0_ffffffff_7fffffffh
	mov rax,0_ffffffff_80000000h

	mov rax,0_11223344_8899aabbh
	mov rax,dword 0_11223344_8899aabbh
	mov eax,0_11223344_8899aabbh
	mov [rax],dword 0_11223344_8899aabbh	; 32-bit operation
	mov qword [rax],0_11223344_8899aabbh
	mov qword [rax],dword 0_11223344_8899aabbh
	
	mov rax,strict 11223344h
	mov rax,strict dword 11223344h
	mov eax,strict 11223344h
	mov [rax],strict dword 11223344h		; 32-bit operation
	mov qword [rax],strict 11223344h
	mov qword [rax],strict dword 11223344h

	mov rax,strict 0_ffffffff_8899aabbh
	mov rax,strict dword 0_ffffffff_8899aabbh
	mov eax,strict 0_ffffffff_8899aabbh
	mov [rax],strict dword 0_ffffffff_8899aabbh	; 32-bit operation
	mov qword [rax],strict 0_ffffffff_8899aabbh
	mov qword [rax],strict dword 0_ffffffff_8899aabbh

	mov rax,strict 7fffffffh
	mov rax,strict 80000000h
	mov rax,strict 0_ffffffffh
	mov rax,strict 1_00000000h
	mov rax,strict 0_ffffffff_7fffffffh
	mov rax,strict 0_ffffffff_80000000h

	mov rax,strict 0_11223344_8899aabbh
	mov rax,strict dword 0_11223344_8899aabbh
	mov eax,strict 0_11223344_8899aabbh
	mov [rax],strict dword 0_11223344_8899aabbh	; 32-bit operation
	mov qword [rax],strict 0_11223344_8899aabbh
	mov qword [rax],strict dword 0_11223344_8899aabbh
	
