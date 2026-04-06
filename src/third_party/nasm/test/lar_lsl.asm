;Testname=test; Arguments=-fbin -olar_lsl.bin; Files=stdout stderr lar_lsl.bin

; LAR/LSL
;---------

; 1x ; = invalid due to lack of REX
; 3x ; = invalid due to Mw

%macro m 1

  bits 16

	%1  ax, ax
	%1  ax,eax
;	%1  ax,rax

	%1 eax, ax
	%1 eax,eax
;	%1 eax,rax

;	%1 rax, ax
;	%1 rax,eax
;	%1 rax,rax

	%1  ax,      [0]
	%1  ax, word [0]
;;;	%1  ax,dword [0]
;	%1  ax,qword [0]

	%1 eax,      [0]
	%1 eax, word [0]
;;;	%1 eax,dword [0]
;	%1 eax,qword [0]

;	%1 rax,      [0]
;	%1 rax, word [0]
;	%1 rax,dword [0]
;	%1 rax,qword [0]

  bits 32

	%1  ax, ax
	%1  ax,eax
;	%1  ax,rax

	%1 eax, ax
	%1 eax,eax
;	%1 eax,rax

;	%1 rax, ax
;	%1 rax,eax
;	%1 rax,rax

	%1  ax,      [0]
	%1  ax, word [0]
;;;	%1  ax,dword [0]
;	%1  ax,qword [0]

	%1 eax,      [0]
	%1 eax, word [0]
;;;	%1 eax,dword [0]
;	%1 eax,qword [0]

;	%1 rax,      [0]
;	%1 rax, word [0]
;	%1 rax,dword [0]
;	%1 rax,qword [0]

  bits 64

	%1  ax, ax
	%1  ax,eax
	%1  ax,rax	; $TODO: shouldn't emit REX.W $

	%1 eax, ax
	%1 eax,eax
	%1 eax,rax	; $TODO: shouldn't emit REX.W $

	%1 rax, ax
	%1 rax,eax
	%1 rax,rax

	%1  ax,      [0]
	%1  ax, word [0]
;;;	%1  ax,dword [0]
;;;	%1  ax,qword [0]

	%1 eax,      [0]
	%1 eax, word [0]
;;;	%1 eax,dword [0]
;;;	%1 eax,qword [0]

	%1 rax,      [0]
	%1 rax, word [0]
;;;	%1 rax,dword [0]
;;;	%1 rax,qword [0]

%endmacro

m lar

m lsl

bits 16
lar ax,[ si]
lar ax,[esi]
bits 32
lar ax,[ si]
lar ax,[esi]
bits 64
lar ax,[esi]
lar ax,[rsi]

bits 16
lsl ax,[ si]
lsl ax,[esi]
bits 32
lsl ax,[ si]
lsl ax,[esi]
bits 64
lar ax,[esi]
lsl ax,[rsi]

; EOF
