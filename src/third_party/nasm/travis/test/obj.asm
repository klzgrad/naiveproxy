;Testname=unoptimized; Arguments=-O0 -fobj -oobj.o; Files=stdout stderr obj.o
;Testname=optimized;   Arguments=-Ox -fobj -oobj.o; Files=stdout stderr obj.o

; test source file for assembling to Microsoft 16-bit .OBJ
; build with (16-bit Microsoft C):
;    nasm -f obj objtest.asm
;    cl /AL objtest.obj objlink.c
; other compilers should work too, provided they handle large
; model in the same way as MS C

; This file should test the following:
; [1] Define and export a global symbol
; [2] Define a non-global symbol
; [3] Define a common symbol
; [4] Define a NASM local label
; [5] Reference a NASM local label
; [6] Import an external symbol
; [7] Make a PC-relative relocated reference
; [8] Reference a symbol in the same section as itself
; [9] Reference a symbol in a different segment from itself
; [10] Define a segment group
; [11] Take the offset of a symbol in a grouped segment w.r.t. its segment
; [12] Reserve uninitialised data space in a segment
; [13] Directly take the segment address of a segment
; [14] Directly take the segment address of a group
; [15] Use SEG on a non-external
; [16] Use SEG on an external

	  bits 16

	  global _bsssym	; [1]
	  global _function	; [1]
	  global _selfptr	; [1]
	  global _selfptr2	; [1]
	  common _commvar 2	; [3]
	  extern _printf	; [6]

	  group mygroup mybss mydata
	  group mygroup2 mycode mycode2

	  segment mycode private

_function push bp
	  mov bp,sp
	  push ds
	  mov ax,mygroup	; [14]
	  mov ds,ax
	  inc word [_bsssym]	; [9]
	  mov ax,seg _commvar
	  mov ds,ax
	  dec word [_commvar]
	  pop ds
	  mov ax,[bp+6]
	  mov dx,[bp+8]
	  push dx
	  push ax
	  push dx
	  push ax
	  call far [cs:.printf]	; [5] [8]
	  pop ax
	  pop ax
	  call trampoline	; [7]
	  pop ax
	  pop ax
	  mov sp,bp
	  pop bp
	  retf

.printf	  dw _printf, seg _printf ; [2] [4] [16]
.printfd  dd _printf, seg _printf ; [2] [4] [16]
.printfq  dq _printf, seg _printf ; [2] [4] [16]

	  segment mycode2 private

trampoline: pop ax
	  push cs
	  push ax
	  jmp far _printf

	  segment mybss private

_bsssym	  resw 64		; [12]

	  segment mydata private

_selfptr  dw _selfptr, seg _selfptr ; [8] [15]
_selfptr2 dw _selfptr2 wrt mydata, mydata ; [11] [13]
