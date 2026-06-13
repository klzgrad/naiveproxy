	section TEXT16

	bits 16
	jmp 1:there
	jmp word 1:there
	jmp 1:word there
	jmp word 1:word there
	jmp dword 1:there
	jmp 1:dword there
	jmp word 1:dword there
	jmp far [bx]
	jmp far word [bx]
	jmp far dword [bx]

	call 1:there
	call word 1:there
	call 1:word there
	call word 1:word there
	call dword 1:there
	call 1:dword there
	call word 1:dword there
	call far there
	call far [bx]
	call far word [bx]
	call far dword [bx]

	section TEXT32

	bits 32
	jmp 1:there
	jmp word 1:there
	jmp 1:word there
	jmp word 1:word there
	jmp dword 1:there
	jmp 1:dword there
	jmp word 1:dword there
	jmp far there
	jmp far [ebx]
	jmp far word [ebx]
	jmp far dword [ebx]

	call 1:there
	call word 1:there
	call 1:word there
	call word 1:word there
	call dword 1:there
	call 1:dword there
	call word 1:dword there
	call far there
	call far [ebx]
	call far word [ebx]
	call far dword [ebx]

	section TARGET
there:
	hlt
