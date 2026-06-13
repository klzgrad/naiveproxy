;Testname=test; Arguments=-fbin -otestdos.bin; Files=stdout stderr testdos.bin
;
; This file was known to miscompile with the 16-bit NASM built
; under Borland C++ 3.1, so keep it around for testing...
;
; The proper output looks like:
;
; 00000000 A10300
; 00000003 EA0000FFFF
;
	org 0100h
	mov ax,[3]
	jmp 0FFFFh:0000
