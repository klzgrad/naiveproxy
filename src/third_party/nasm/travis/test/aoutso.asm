; test source file for assembling to NetBSD/FreeBSD a.out shared library
; build with:
;    nasm -f aoutb aoutso.asm
;    ld -Bshareable -o aoutso.so aoutso.o
; test with:
;    cc -o aoutso aouttest.c aoutso.so
;    ./aoutso

; This file should test the following:
; [1] Define and export a global text-section symbol
; [2] Define and export a global data-section symbol
; [3] Define and export a global BSS-section symbol
; [4] Define a non-global text-section symbol
; [5] Define a non-global data-section symbol
; [6] Define a non-global BSS-section symbol
; [7] Define a COMMON symbol
; [8] Define a NASM local label
; [9] Reference a NASM local label
; [10] Import an external symbol
; [11] Make a PC-relative call to an external symbol
; [12] Reference a text-section symbol in the text section
; [13] Reference a data-section symbol in the text section
; [14] Reference a BSS-section symbol in the text section
; [15] Reference a text-section symbol in the data section
; [16] Reference a data-section symbol in the data section
; [17] Reference a BSS-section symbol in the data section

	  BITS 32
	  EXTERN __GLOBAL_OFFSET_TABLE_
	  GLOBAL _lrotate:function ; [1]
	  GLOBAL _greet:function ; [1]
	  GLOBAL _asmstr:data _asmstr.end-_asmstr ; [2]
	  GLOBAL _textptr:data 4 ; [2]
	  GLOBAL _selfptr:data 4 ; [2]
	  GLOBAL _integer:data 4 ; [3]
	  EXTERN _printf	; [10]
	  COMMON _commvar 4	; [7]

	  SECTION .text

; prototype: long lrotate(long x, int num);
_lrotate:			; [1]
	  push ebp
	  mov ebp,esp
	  mov eax,[ebp+8]
	  mov ecx,[ebp+12]
.label	  rol eax,1		; [4] [8]
	  loop .label		; [9] [12]
	  mov esp,ebp
	  pop ebp
	  ret

; prototype: void greet(void);
_greet	  push ebx		; we'll use EBX for GOT, so save it
	  call .getgot
.getgot:  pop ebx
	  add ebx,__GLOBAL_OFFSET_TABLE_ + $$ - .getgot wrt ..gotpc
	  mov eax,[ebx+_integer wrt ..got] ; [14]
	  mov eax,[eax]
	  inc eax
	  mov [ebx+localint wrt ..gotoff],eax ; [14]
	  mov eax,[ebx+_commvar wrt ..got]
	  push dword [eax]
	  mov eax,[ebx+localptr wrt ..gotoff] ; [13]
	  push dword [eax]
	  mov eax,[ebx+_integer wrt ..got] ; [1] [14]
	  push dword [eax]
	  lea eax,[ebx+_printfstr wrt ..gotoff]
	  push eax		; [13]
	  call _printf wrt ..plt ; [11]
	  add esp,16
	  pop ebx
	  ret

	  SECTION .data

; a string
_asmstr	  db 'hello, world', 0	; [2]
.end

; a string for Printf
_printfstr db "integer==%d, localint==%d, commvar=%d"
	  db 10, 0

; some pointers
localptr  dd localint		; [5] [17]
_textptr  dd _greet wrt ..sym	; [15]
_selfptr  dd _selfptr wrt ..sym	; [16]

	  SECTION .bss

; an integer
_integer  resd 1		; [3]

; a local integer
localint  resd 1		; [6]
