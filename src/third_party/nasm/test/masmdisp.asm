	%use masm

	global fproc, nproc

	bits 64

_TEXT	segment

fproc	proc far
	mov eax,dword ptr foo
	mov rdx,offset foo
	mov ecx,bar[rbx]
	lea rsi,foo
	lea rsi,dword ptr foo
	lea rsi,[foo]
	lea rsi,dword [foo]
	mov rdi,gs:[rbx]
	ret
fproc	endp

nproc	proc near
	mov eax,dword ptr foo
	mov rdx,offset foo
	mov ecx,bar[rbx]
	mov rdi,[gs:foo]
	mov rdi,qword ptr gs:foo
	ret
nproc	endp

_TEXT	ends

_DATA	segment
nxx	dd 80
foo	dd 100
_DATA	ends

	segment _BSS nobits
bar	resd 100
xyzzy	dd 64 dup (?)
_BSS	ends
