; This file, plus inc1.asm and inc2.asm, test NASM's file inclusion
; mechanism.
;
; This produces a DOS .COM file: to assemble, use
;    nasm -f bin inctest.asm -o inctest.com
; and when run, it should print `hello, world'.

	  BITS 16
	  ORG 0x100

	  jmp _main

%include "inc1.asm"
