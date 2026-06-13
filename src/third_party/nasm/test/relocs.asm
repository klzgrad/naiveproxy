	bits 64
	extern foo

	mov eax,[foo]
	mov rax,[foo]
	mov rax,[qword foo]

	mov eax,[a32 foo]
	mov rax,[a32 foo]
	mov rax,[a32 qword foo]

	mov eax,foo
	mov rax,dword foo
	mov rax,qword foo
	mov eax,foo
	mov rax,dword foo
	mov rax,qword foo

	dd foo
	dq foo
