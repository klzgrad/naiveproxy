; Demonstration of how to write an entire .EXE format program as a .OBJ
; file to be linked. Tested with the VAL free linker.
; To build:
;    nasm -fobj objexe.asm
;    val objexe.obj,objexe.exe;
; To test:
;    objexe
; (should print `hello, world')
	  
	  segment code

..start:  mov ax,data
	  mov ds,ax
	  mov ax,stack
	  mov ss,ax
	  mov sp,stacktop

	  mov dx,hello
	  mov ah,9
	  int 0x21

	  mov ax,0x4c00
	  int 0x21

	  segment data
hello:	  db 'hello, world', 13, 10, '$'

	  segment stack stack
	  resb 64
stacktop:
