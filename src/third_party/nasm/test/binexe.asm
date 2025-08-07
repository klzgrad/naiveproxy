;Testname=unoptimized; Arguments=-O0 -fbin -obinexe.exe -i../misc/; Files=stdout stderr binexe.exe
;Testname=optimized;   Arguments=-Ox -fbin -obinexe.exe -i../misc/; Files=stdout stderr binexe.exe

; Demonstration of how to write an entire .EXE format program by using
; the `exebin.mac' macro package.
; To build:
;    nasm -fbin binexe.asm -o binexe.exe -ipath
; (where `path' is such as to allow the %include directive to find
; exebin.mac)
; To test:
;    binexe
; (should print `hello, world')

%include "exebin.mac"

	  EXE_begin
	  EXE_stack 64		; demonstrates overriding the 0x800 default

	  section .text

	  mov ax,cs
	  mov ds,ax

	  mov dx,hello
	  mov ah,9
	  int 0x21

	  mov ax,0x4c00
	  int 0x21

	  section .data

hello:	  db 'hello, world', 13, 10, '$'

	  EXE_end
