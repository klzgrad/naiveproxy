; nasm -O99 -f elf32 floattest.asm
; ld -m elf_i386 -o floattest floattest.o -I/lib/ld-linux.so.2 -lc

	global _start
	extern printf

	section .text
_start:

	fld qword [num1]
	fadd qword [num2]
	sub esp, 8
	fstp qword [esp]
	push fmt
	call printf
	add esp, 4*3

	mov eax, 1
	xor ebx, ebx
	int 80h

	section .data
num1	dq 41.5
num2	dq 0.5

fmt	db "%f", 10, 0
