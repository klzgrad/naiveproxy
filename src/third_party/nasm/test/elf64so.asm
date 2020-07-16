; test source file for assembling to ELF64 shared library
; build with:
;    nasm -f elf64 elf64so.asm
;    ld -shared -o elf64so.so elf64so.o
; test with:
;    gcc -o elf64so elftest64.c ./elf64so.so
;    ./elf64so

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

	  BITS 64
	  GLOBAL lrotate:function ; [1]
	  GLOBAL greet_s:function ; [1]
	  GLOBAL greet_m:function ; [1]
	  GLOBAL asmstr:data asmstr.end-asmstr ; [2]
	  GLOBAL textptr:data 8	; [2]
	  GLOBAL selfptr:data 8	; [2]
	  GLOBAL useless:data 8	; [3]
	  GLOBAL integer:data 8	; [3]
	  EXTERN printf		; [10]
	  COMMON commvar 8:8	; [7]
	  EXTERN _GLOBAL_OFFSET_TABLE_

	  SECTION .text

; prototype: long lrotate(long x, int num);
lrotate:			; [1]
	  push rbp
	  mov rbp,rsp
	  mov rax,rdi
	  mov rcx,rsi
.label	  rol rax,1		; [4] [8]
	  loop .label		; [9] [12]
	  mov rsp,rbp
	  pop rbp
	  ret

;; prototype: void greet_*(void);
;; 
;;  Arguments are:	rdi - rsi - rdx - rcx - r8 - r9
;;  Registers:		rbx, rbp, r12-r15 are saved
;; greet_s() is Small PIC model, greet_m() is Medium PIC model
;; (Large model cannot be linked with other code)
;;
greet_s:
	  ;;  This instruction is useless, this is only a test...
	  cmp qword [rel integer wrt ..got],0
	  mov rax,[rel commvar wrt ..got] ; &commvar
	  mov rcx,[rax]			  ; commvar
	  mov rax,[rel integer wrt ..got] ; &integer
	  mov rsi,[rax]
	  lea rdx,[rsi+1]
	  mov [rel localint],rdx ; localint = integer+1
	  mov rax,[rel localptr] ; localptr
	  mov rdx,[rax]		 ; *localptr = localint
	  lea rdi,[rel printfstr]
	  xor eax,eax		; No fp arguments
	  jmp printf wrt ..plt	; [10]

greet_m:
	  push r15		; Used by convention...
	  lea r15,[rel _GLOBAL_OFFSET_TABLE_]
	  mov rax,[rel commvar wrt ..got] ; &commvar
	  mov rcx,[rax]			  ; commvar
	  mov rax,[rel integer wrt ..got] ; &integer
	  mov rsi,[rax]
	  lea rdx,[rsi+1]
	  mov rax,localint wrt ..gotoff	 ; &localint - r15
	  mov [rax+r15],rdx	 ; localint = integer+1
	  mov rax,localptr wrt ..gotoff ; &localptr - r15
	  mov rax,[rax+r15]	 ; localptr
	  mov rdx,[rax]		 ; *localptr = localint
	  mov rdi,printfstr wrt ..gotoff ; &printfstr - r15
	  add rdi,r15		; &printfstr 
	  xor eax,eax		; No fp arguments
	  pop r15
	  jmp printf wrt ..plt	; [10]

	  SECTION .data

; a string
asmstr	  db 'hello, world', 0	; [2]
.end:

; a string for Printf
printfstr db "integer=%ld, localint=%ld, commvar=%ld", 10, 0

; some pointers
localptr  dq localint		; [5] [17]
textptr	  dq greet_s wrt ..sym	; [15]
selfptr	  dq selfptr wrt ..sym	; [16]

	  SECTION .bss
; a useless symbol
useless	  resq 1
	
; an integer
integer	  resq 1		; [3]

; a local integer
localint  resq 1		; [6]
