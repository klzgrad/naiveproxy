;Testname=aout;  Arguments=-faout  -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=aoutb; Arguments=-faoutb -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=as86;  Arguments=-fas86  -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=elf32; Arguments=-felf32 -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=elf64; Arguments=-felf64 -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=obj;   Arguments=-fobj   -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=rdf;   Arguments=-frdf   -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=win32; Arguments=-fwin32 -olnxhello.o -Ox; Files=stdout stderr lnxhello.o
;Testname=win64; Arguments=-fwin64 -olnxhello.o -Ox; Files=stdout stderr lnxhello.o

; To test where code that is placed before any explicit SECTION
; gets placed, and what happens if a .text section has an ORG
;statement, uncomment the following lines.
;
;	times	10h	nop
;
;section .text
;org 0x300
;	times	20h	inc ax

; let's see which of these sections can be placed in the specified order.

section .appspecific
section .data
section .stringdata
section .mytext
section .code
section .extra_code


section .stringdata
mystr1: db "Hello, this is string 1", 13, 10, '$'

section .extra_code
;org 0x200
bits 16
more:
   mov si, asciz1
   mov ah, 0x0E
   xor bx, bx
.print:
   lodsb
   test al, al
   jz .end
   int  0x10
   jmp short .print
.end:

   xor ax, ax
   int 0x16

   mov ax, 0x4c00
   int 0x21

section .appspecific
asciz1: db "This is string 2", 0

section .code
;org 0x100
bits 16

start:
   mov dx, mystr1
   mov ah, 9
   int 0x21

   xor ax, ax
   int 0x16

   jmp more

section .text
	xor	eax,eax
	times	50h nop

section .mytext

	xor	ebx,ebx

section .data
	db	95h,95h,95h,95h,95h,95h,95h,95h

section .hmm
	resd	2

section .bss
	resd	8

section .final1
	inc	ax

section .final2
	inc	bx

section .final3
	inc	cx
