	bits 64
	default rel

%if 1
	extern bar
%else
	section .bss
bar:	resd 0
%endif

	global start
	global foo

	section .rodata
rod1:	dd 0x01234567
rod2:	dd 0x89abcdef

	section .text
start:
	call .next
.next:	pop rsi
	sub rsi,.next-$$

	lea rax, [rod1]
	lea rcx, [rod2]
	lea rdx, [bar]
	lea rbx, [foo]
	
	lea rax, [rdi+rod1-$$]
	lea rcx, [rdi+rod2-$$]
	lea rdx, [rdi+bar-$$]
	lea rbx, [rdi+foo-$$]
	
	mov rax, [rdi+rod1-$$]
	mov rcx, [rdi+rod2-$$]
	mov rdx, [rdi+bar-$$]
	mov rbx, [rdi+foo-$$]

	mov rax, dword rod1-$$
	mov rcx, dword rod2-$$
	mov rdx, dword bar-$$
	mov rbx, dword foo-$$
	
	section .data
	dq rod1
	dq rod2
	dq bar
	dq foo
foo:
	dd rod1 - $
	dd rod1 - $$
	dd rod2 - $
	dd rod2 - $$
	dd bar - $
	dd bar - $$
	dd foo - $
	dd foo - $$
