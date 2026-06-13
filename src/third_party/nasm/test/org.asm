;Testname=elf64; Arguments=-Ox -felf64 -oorg.o; Files=stdout stderr org.o
;Testname=win64; Arguments=-Ox -fwin64 -oorg.o; Files=stdout stderr org.o

;
; Simple test of a 64-bit org directive
; 
		bits 64
		org 0xffffffffffff0000

hello:		jmp there
		nop
		nop
there:
		add rax,[rsp+rbx]
		inc eax

		section .data
there_ptr	dq there
