;This file demonstrates many of the differences between NASM version X and NASM
;version 0.97
;
; changed.asm is copyright (C) 1998 John S. Fine
;
;  It may be redistributed under the same conditions as NASM as described in
;  LICENSE file in the NASM archive
;_________________________________
;
;  nasm changed.asm -l changed.lst
;
; When assembled without any -d switches, it includes examples which:
;       Work correctly in version X
;  and  Work incorrectly and/or display warnings in version 0.97
;  and  Do not prevent the generation of output in version 0.97
;
; Not all the differences can be seen in the .lst file.  I suggest that you use
; "ndisasm changes"  to examine the code actually generated.
;_________________________________
;
;  nasm changed.asm -l changed.lst -doldmsg
;
; When assembled with -doldmsg, it adds examples which:
;       Work correctly in version X
;  and  Generate error messages in version 0.97 and do not generate output
;_________________________________
;
;  nasm changed.asm -l changed.lst -doldcrash
;
; When assembled with -doldcrash, it adds examples which:
;       Work correctly in version X
;  and  Cause NASM to crash in version 0.97
;_________________________________
;
;  nasm changed.asm -l changed.lst -dnewmsg
;
; When assembled with -dnewmsg, it adds examples which:
;       Generate error messages in version X
;  and  Generate wrong output without warning or error message in version 0.97
;-----------------------------------------------------------------------------

; Please note that I have reported the name of the person who made the
; correction based on very limited information.  In several cases, I am sure I
; will identify the wrong author.  Please send me any corrections;  I don't
; intend to insult or exclude anyone.

;-----------------------------------------------------------------------------
; Bug fixed by Simon in assemble()
;
; The following generated "call next" / "call next-1" instead of
; two copies of "call next"
;
	times 2 a16 call next
next:

;-----------------------------------------------------------------------------
; Bug fixed by John in parse_line()  (and other routines)
;
; This used to jmp to prior.1, when it should be here.1
;
prior:
.1:
here:	jmp	.1
.1:

;-----------------------------------------------------------------------------
; Bug fixed by John in assemble()
;
; Strings used in dq and dt were not zero filled correctly
;
	dq	'b'


;-----------------------------------------------------------------------------
; Bug fixed by Simon in isn_names[]
;
; Was not recognised as an instruction
;
	int01			; Instead of INT1

;-----------------------------------------------------------------------------
; Bug fixed by Jim Hague in ???
;
; Forward references were instruction level rather than per operand
;
	shr word [forwardref],1
forwardref:

;-----------------------------------------------------------------------------
; Bug fixed by John in preproc.c
;
; It used to silently discard id characters appended to a multi-line
; macro parameter (such as the x in %1x below).
;
%macro xxx 1
%1: nop
%{1}x: jmp %1x
%endmacro
xxx yyy

;-----------------------------------------------------------------------------
; Bug added by John in preproc.c 0.98-J4, removed by John in 0.98-J5
;
; Tested here to make sure it stays removed
;
%macro TestElse 1
%if %1=0
%elif %1=1
nop
%endif
%endmacro
TestElse 1

%ifdef oldmsg
;***************************************************************
;
; The following examples will generate error messages in 0.97 and will generate
; correct output in the new version.

;-----------------------------------------------------------------------------
; Bug fixed by Simon in isns.dat
;
; The optional "near" was not permitted on JMP and CALL
;
	jmp near here

;-----------------------------------------------------------------------------
; Feature added by Simon in stdscan()
;
; You can now use the numeric value of strings in %assign
;
%assign xxx 'ABCD'
	dd xxx

;-----------------------------------------------------------------------------
; Feature added by John in add_vectors()
;
; Stranger address expressions are now supported as long as they resolve to
; something valid.
;
	mov ax, [eax + ebx + ecx - eax]

;-----------------------------------------------------------------------------
; Bug fixed by Simon in ???
;
; The EQU directive affected local labels in a way that was inconsistent
; between passes
;
.local:
neither equ $
	jmp .local

;-----------------------------------------------------------------------------
; Feature added by Jules in parse_line
;
; You can override a size specifier
;
%define arg1 dword [bp+4]
	cmp word arg1, 2

;-----------------------------------------------------------------------------
; Bug fixed by John in preproc.c
;
; You could not use a label on the same line with a macro invocation, if the
; macro definition began with a preprocessor directive.
;
	struc mytype
.long	resd	1
	endstruc

lbl	istruc mytype
	at mytype.long, dd 'ABCD'
	iend

;-----------------------------------------------------------------------------
; Warning removed by John in preproc.c
;
; In order to allow macros that extend the definition of instructions, I
; disabled the warning on a multi-line macro referencing itself.
;
%endif			;NASM 0.97 doesn't handle %0 etc. inside false %if
%macro push 1-*		;
%rep %0			;
push %1			;
%rotate 1		;
%endrep			;
%endmacro		;
%ifdef oldmsg		;

	push ax,bx

;-----------------------------------------------------------------------------
; Warning removed by John in preproc.c
;
; To support other types of macros that extend the definition of instructions,
; I disabled the warning on a multi-line macro called with the wrong number of
; parameters.  PUSH and POP can be extended equally well by either method, but
; other instruction extensions may need one method or the other, so I made both
; work.
;
; Note that neither of these warnings was really needed, because a later stage
; of NASM would almost always give an adequate error message if the macro use
; really was wrong.
;
%endif
%macro pop 2-*
%rep %0
pop %1
%rotate 1
%endrep
%endmacro
%ifdef oldmsg

	pop ax,bx
%endif


%ifdef newmsg  ;***************************************************************

;-----------------------------------------------------------------------------
; Bug fixed by John in parse_line()  (and other routines)
;
; This invalid code used to assemble without errors
;
myself equ myself+1
	jmp myself

;-----------------------------------------------------------------------------
; Change made by John in preproc.c
;
; In 0.97, an id that appears as a label on a macro invocation was always
; prepended to the first line of the macro expansion.  That caused several
; bugs, but also could be used in tricks like the arg macro in c16.mac and
; c32.mac.
;
; In version X, an id that appears as a label on a macro invocation will
; normally be defined as a label for the address at which the macro is
; invoked, regardless of whether the first line of the macro expansion is
; something that can take a label.  The new token %00 may be used for any
; of the situations in which the old prepend behavior was doing something
; tricky but useful.  %00 can also be used more than once and in places
; other than the start of the expansion.
;
%endif
%assign arg_off 0

%imacro arg 0-1 2		;arg defined the old way
	  equ arg_off
%assign arg_off %1+arg_off
%endmacro

%ifdef newmsg
arg_example arg
%endif

%imacro arg2 0-1 2		;arg defined the new way
%00	  equ arg_off
%assign arg_off %1+arg_off
%endmacro

%ifdef oldmsg
arg_example2 arg2

;-----------------------------------------------------------------------------
; Change made by Jules and John in INSNS.DAT
;
; Various instruction in which the size of an immediate is built-in to the
; instruction set, now allow you to redundantly specify that size as long
; as you specify it correctly
;
	AAD	byte 5
	AAM	byte 5
	BT	bx, byte 3
	BTC	cx, byte 4
	BTR	dx, byte 5
	BTS	si, byte 6
	IN	eax, byte 0x40
	INT	byte 21h
	OUT	byte 70h, ax
	RET	word 2
	RETN	word 2
	RETF	word 4

; note "ENTER" has not been changed yet.

;-----------------------------------------------------------------------------
; Enhancement by hpa in insns.dat et al
;
; Simplified adding new instructions, and added some missing instructions
;
	int03			; Instead of INT3
	ud1			; No documented mnemonic for this one
	ud2
	sysenter
	sysexit
	syscall
	sysret
	fxsave [ebx]
	fxrstor [es:ebx+esi*4+0x3000]

;-----------------------------------------------------------------------------
; Enhancement by hpa in insns.dat et al
;
; Actually make SSE work, and use the -p option to ndisasm to select
; one of several aliased opcodes
;
	sqrtps xmm0,[ebx+10]	; SSE opcode
	paddsiw mm0,[ebx+10]	; Cyrix opcode with the same byte seq.

;-----------------------------------------------------------------------------
; Enhancement by hpa in preproc.c
;
; Support %undef to remove a single-line macro
;
%define	TEST_ME 42
%ifndef TEST_ME
%error	"TEST_ME not defined after %define"
%endif

%undef  TEST_ME
%ifdef  TEST_ME
%error	"TEST_ME defined after %undef"
%endif

;-----------------------------------------------------------------------------
; Bug fix by hpa in insns.dat
;
; PSHUFW and PINSRW weren't handling the implicit sizes correctly; all of
; the entries below are (or should be) legal
;
	pshufw mm2, mm1, 3
	pshufw mm3,[ebx],2
	pshufw mm7,[0+edi*8],1

	pshufw mm2, mm1, byte 3
	pshufw mm3,[ebx],byte 2
	pshufw mm7,[0+edi*8],byte 1

	pshufw mm2, mm1, 3
	pshufw mm3, qword [ebx], 2
	pshufw mm7, qword [0+edi*8], 1

	pshufw mm2, mm1, byte 3
	pshufw mm3, qword [ebx], byte 2
	pshufw mm7, qword [0+edi*8], byte 1

	pinsrw mm1, [esi], 1
	pinsrw mm1, word [esi], 1
	pinsrw mm1, [esi], byte 1
	pinsrw mm1, word [esi], byte 1


%endif				; oldmsg

%ifdef oldcrash  ;*************************************************************

This_label_is_256_characters_long__There_used_to_be_a_bug_in_stdscan_which_made_it_crash_when_it_did_a_keyword_search_on_any_label_longer_than_255_characters__Now_anything_longer_than_MAX_KEYWORD_is_always_a_symbol__It_will_not_even_try_a_keyword_search___

;-----------------------------------------------------------------------------
; Bug fixed by John in preproc.c
;
; Builds of NASM that prohibit dereferencing a NULL pointer used to crash if a
; macro that started with a blank line was invoked with a label
;
%macro empty_macro 0

%endm

emlabel empty_macro
	jmp	emlabel

;-----------------------------------------------------------------------------
; Enhancement by Conan Brink in preproc.c
;
; Allow %rep to be nested
;
%rep 4
%rep 5
	nop
%endrep
%endrep

%endif
