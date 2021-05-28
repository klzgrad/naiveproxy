; This file is part of the include test.
; See inctest.asm for build instructions.

_main:	  mov dx,message
	  mov ah,9
	  int 21h
	  mov ax,4c00h
	  int 21h
