	bits 64
_start:
	es add eax,eax
	add eax,[rdx]
	add eax,[fs:rdx]
	fs add eax,[rdx]
	movzx eax,word [fs:rdx]
	jz foo
	ds jz foo
	jmp bar
	db 0x48
	jmp bar
	cs jmp bar
	jmp strict near bar
	cs jmp strict near bar
	mov eax,[r15]
	mov eax,[r31]
	mov rax,[rdx]
	mov rax,[r15]
	mov rax,[r31]
foo:
	hlt
bar:
	udb
