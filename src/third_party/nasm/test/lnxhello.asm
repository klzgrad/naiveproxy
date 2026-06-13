;Testname=aout;  Arguments=-faout  -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=aoutb; Arguments=-faoutb -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=as86;  Arguments=-fas86  -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=elf32; Arguments=-felf32 -olnxhello.o -Ox; Files=stdout stderr lnxhello.o

;
; Assembly "Hello, World!" for Linux
;


; Properly defined in <sys/syscall.h>
%define SYS_exit	1
%define SYS_write	4

	section .text

	global _start
_start:
	; gdb doesn't like to stop at the entry point address, so
	; we put a nop here for pure convenience
	nop				


write_hello:
	mov edx, hello_len
	mov ecx, hello
	
.loop:
	mov eax, SYS_write
	mov ebx, 1			; stdout
	int 80h

	cmp eax, -4096
	ja error

	add ecx, eax
	sub edx, eax
	jnz .loop

ok:	
	mov eax, SYS_exit
	xor ebx, ebx
	int 80h
	hlt

error:
	mov eax, SYS_exit
	mov ebx, 1		; Error
	int 80h
	hlt
	
	section .rodata
hello:	db "Hello, World!", 10
hello_len equ $-hello
