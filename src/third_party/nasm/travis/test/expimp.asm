;Testname=O0; Arguments=-O0 -fbin -oexpimp.bin; Files=stdout stderr expimp.bin
;Testname=O1; Arguments=-O1 -fbin -oexpimp.bin; Files=stdout stderr expimp.bin
;Testname=Ox; Arguments=-Ox -fbin -oexpimp.bin; Files=stdout stderr expimp.bin
;Testname=error-O0; Arguments=-O0 -fbin -oexpimp.bin -DERROR; Files=stdout stderr expimp.bin
;Testname=error-Ox; Arguments=-Ox -fbin -oexpimp.bin -DERROR; Files=stdout stderr expimp.bin

;
; Test of explicitly and implicitly sized operands
;
	BITS 32

	add esi,2			; Implicit
	add esi,123456h			; Implicit
	add esi,byte 2			; Explicit
	add esi,dword 2			; Explicit
	add esi,dword 123456h		; Explicit
	add esi,byte 123456h		; Explicit Truncation

	add esi,strict 2		; Implicit Strict
	add esi,strict 123456h		; Implicit Strict
	add esi,strict byte 2		; Explicit Strict
	add esi,strict dword 2		; Explicit Strict
	add esi,strict dword 123456h	; Explicit Strict
	add esi,strict byte 123456h	; Explicit Strict Truncation

	add eax,2			; Implicit
	add eax,123456h			; Implicit
	add eax,byte 2			; Explicit
	add eax,dword 2			; Explicit
	add eax,dword 123456h		; Explicit
	add eax,byte 123456h		; Explicit Truncation

	add eax,strict 2		; Implicit Strict
	add eax,strict 123456h		; Implicit Strict
	add eax,strict byte 2		; Explicit Strict
	add eax,strict dword 2		; Explicit Strict
	add eax,strict dword 123456h	; Explicit Strict
	add eax,strict byte 123456h	; Explicit Strict Truncation

	imul dx,3			; Implicit
	imul dx,byte 3			; Explicit
	imul dx,word 3			; Explicit
	imul dx,strict byte 3		; Explicit Strict
	imul dx,strict word 3		; Explicit Strict

;
; Same thing with branches
;
start:
	jmp short start			; Explicit
	jmp near start			; Explicit
	jmp word start			; Explicit
	jmp dword start			; Explicit
	jmp short forward		; Explicit
	jmp near forward		; Explicit
	jmp word forward		; Explicit
	jmp dword forward		; Explicit
%ifdef ERROR
	jmp short faraway		; Explicit (ERROR)
%endif
	jmp near faraway		; Explicit
	jmp word faraway		; Explicit
	jmp dword faraway		; Explicit
	jmp start			; Implicit
	jmp forward			; Implicit
	jmp faraway			; Implicit

	jmp strict short start		; Explicit Strict
	jmp strict near start		; Explicit Strict
	jmp strict word start		; Explicit Strict
	jmp strict dword start		; Explicit Strict
	jmp strict short forward	; Explicit Strict
	jmp strict near forward		; Explicit Strict
	jmp strict word forward		; Explicit Strict
	jmp strict dword forward	; Explicit Strict
%ifdef ERROR
	jmp strict short faraway	; Explicit (ERROR)
%endif
	jmp strict near faraway		; Explicit Strict
	jmp strict word faraway		; Explicit Strict
	jmp strict dword faraway	; Explicit Strict
	jmp strict start		; Implicit Strict
	jmp strict forward		; Implicit Strict
	jmp strict faraway		; Implicit Strict
forward:

	times 256 nop
faraway:


