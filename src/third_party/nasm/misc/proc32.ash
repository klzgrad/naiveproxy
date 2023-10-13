;--------=========xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=========--------
;
;   Copyright (C) 1999 by Andrew Zabolotny
;   Miscelaneous NASM macros that makes use of new preprocessor features
; 
;   This library is free software; you can redistribute it and/or
;   modify it under the terms of the GNU Library General Public
;   License as published by the Free Software Foundation; either
;   version 2 of the License, or (at your option) any later version.
; 
;   This library is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;   Library General Public License for more details.
; 
;   You should have received a copy of the GNU Library General Public
;   License along with this library; if not, write to the Free
;   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
;
;--------=========xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx=========--------

;   The macros in this file provides support for writing 32-bit C-callable
;   NASM routines. For a short description of every macros see the
;   corresponding comment before every one. Simple usage example:
;
;	proc	sin,1
;		targ	%$angle
;		fld	%$angle
;		fsin
;	endproc	sin

%ifndef __PROC32_ASH__
%define __PROC32_ASH__

[WARNING -macro-selfref]

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Mangle a name to be compatible with the C compiler
; Arguments:
;   The name
; Example:
;		cname (my_func)
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%ifdef EXTERNC_UNDERSCORE
		%define	cname(x) _ %+ x
%else
		%define	cname(x) x
%endif

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Import an external C procedure definition
; Arguments:
;   The name of external C procedure
; Example:
;		cextern	printf
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		cextern	1
		%xdefine %1 cname(%1)
	%ifidni __OUTPUT_FORMAT__,obj
		extern	%1:wrt FLAT
	%else
		extern	%1
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Export an C procedure definition
; Arguments:
;   The name of C procedure
; Example:
;		cglobal	my_printf
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		cglobal	1
		%xdefine %1 cname(%1)
		global	%1
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Misc macros to deal with PIC shared libraries
; Comment:
;   Note that we have a different syntax for working with and without
;   PIC shared libraries. In a PIC environment we should load first
;   the address of the variable into a register and then work through
;   that address, i.e: mov eax,myvar; mov [eax],1
;   In a non-PIC environment we should directly write: mov myvar,1
; Example:
;		extvar	myvar
;		GetGOT
;	%ifdef PIC
;		mov	ebx,myvar	; get offset of myvar into ebx
;	%else
;		lea	ebx,myvar
;	%endif
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%ifdef PIC
		cextern	_GLOBAL_OFFSET_TABLE_
	%macro	GetGOT	0
		%ifdef .$proc.stkofs
			%assign .$proc.stkofs .$proc.stkofs+4
		%endif
		call	%$Get_GOT
	%$Get_GOT:
		pop	ebx
		add	ebx,_GLOBAL_OFFSET_TABLE_ + $$ - %$Get_GOT wrt ..gotpc
	%endmacro
	%macro	extvar	1
		cextern	%1
		%xdefine %1 [ebx+%1 wrt ..got]
	%endmacro
%else
	%define	GetGOT
	%macro	extvar	1
		cextern	%1
	%endmacro
%endif

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Begin a procedure definition
;   For performance reasons we don't use stack frame pointer EBP,
;   instead we're using the [esp+xx] addressing. Because of this
;   you should be careful when you work with stack pointer.
;   The push/pop instructions are macros that are defined to
;   deal correctly with these issues.
; Arguments:
;   First argument - the procedure name
;   Second optional argument - the number of bytes for local variables
;   The following arguments could specify the registers that should be
;   pushed at beginning of procedure and popped before exiting
; Example:
;   proc	MyTestProc
;   proc	MyTestProc,4,ebx,esi,edi
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		proc	1-3+ 0
		cglobal	%1
		%push	%1
		align	16
%1:
		%xdefine %$proc.name %1
	; total size of local arguments
		%assign %$proc.locsize (%2+3) & 0xFFFC
	; offset from esp to argument
		%assign	%$proc.argofs 4+%$proc.locsize
	; additional offset to args (tracks push/pops)
		%assign	.$proc.stkofs 0
	; offset from esp to local arguments
		%assign %$proc.locofs 0
	; Now push the registers that we should save
		%define %$proc.save %3
	%if %$proc.locsize != 0
		sub	esp,%$proc.locsize
	%endif
		push	%$proc.save
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Declare an argument passed on stack
;   This macro defines two additional macros:
;     first (with the name given by first argument) - [esp+xx]
;     second (with a underscore appended to first argument) - esp+xx
; Arguments:
;   First argument defines the procedure argument name
;   Second optional parameter defines the size of the argument
;   Default value is 4 (a double word)
; Example:
;		arg	.my_float
;		arg	.my_double,8
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		arg	1-2 4
	%ifndef %$proc.argofs
		%error	"`arg' not in a proc context"
	%else
	; Trick: temporary undefine .$proc.stkofs so that it won't be expanded
		%assign	%%. .$proc.stkofs
		%undef .$proc.stkofs
		%xdefine %{1}_ esp+%$proc.argofs+.$proc.stkofs
		%xdefine %1 [esp+%$proc.argofs+.$proc.stkofs]
		%assign .$proc.stkofs %%.
		%assign %$proc.argofs %2+%$proc.argofs
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Declare an local variable
;     first (with the name given by first argument) - [esp+xx]
;     second (with  a slash prefixing the first argument) - esp+xx
; Arguments:
;   First argument defines the procedure argument name
;   Second optional parameter defines the size of the argument
;   Default value is 4 (a double word)
; Example:
;		loc	.int_value
;		loc	.double_value,8
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		loc	1-2 4
	%ifndef %$proc.locofs
		%error	"`loc' not in a proc context"
	%elif %$proc.locofs + %2 > %$proc.locsize
		%error	"local stack space exceeded"
	%else
		%assign	%%. .$proc.stkofs
		%undef .$proc.stkofs
		%xdefine %{1}_ esp+%$proc.locofs+.$proc.stkofs
		%xdefine %1 [esp+%$proc.locofs+.$proc.stkofs]
		%assign .$proc.stkofs %%.
		%assign %$proc.locofs %$proc.locofs+%2
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Get the type of given size into context-local variable %$type
; Arguments:
;   Size of type we want (1,2,4,8 or 10)
; Example:
;		type	4	; gives "dword"
;		type	10	; gives "tword"
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		type	1
	%if %1 = 1
		%define	%$type byte
	%elif %1 = 2
		%define	%$type word
	%elif %1 = 4
		%define	%$type dword
	%elif %1 = 8
		%define	%$type qword
	%elif %1 = 10
		%define	%$type tword
	%else
		%define %$. %1
		%error "unknown type for argument size %$."
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Same as `arg' but prepends "word", "dword" etc (typed arg)
;     first (with the name given by first argument) - dword [esp+xx]
;     second (with  a slash prefixing the first argument) - esp+xx
; Arguments:
;   Same as for `arg'
; Example:
;		targ	.my_float	; .my_float is now "dword [esp+xxx]"
;		targ	.my_double,8	; .my_double is now "qword [esp+xxx]"
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		targ	1-2 4
	%ifndef %$proc.argofs
		%error	"`targ' not in a proc context"
	%else
		arg	%1,%2
		type	%2
		%assign	%%. .$proc.stkofs
		%undef .$proc.stkofs
		%xdefine %1 %$type %1
		%assign .$proc.stkofs %%.
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Same as `loc' but prepends "word", "dword" etc (typed loc)
;     first (with the name given by first argument) - dword [esp+xx]
;     second (with  a slash prefixing the first argument) - esp+xx
; Arguments:
;   Same as for `loc'
; Example:
;		tloc	int_value
;		tloc	double_value,8
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		tloc	1-2 4
	%ifndef %$proc.locofs
		%error	"`tloc' not in a proc context"
	%else
		loc	%1,%2
		type	%2
		%assign	%%. .$proc.stkofs
		%undef .$proc.stkofs
		%xdefine %1 %$type %1
		%assign .$proc.stkofs %%.
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Finish a procedure
;   Gives an error if proc/endproc pairs mismatch
;   Defines an label called __end_(procedure name)
;   which is useful for calculating function size
; Arguments:
;   (optional) The name of procedure
; Example:
;   endproc	MyTestProc
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%push	tmp	; trick: define a dummy context to avoid error in next line
%macro		endproc	0-1 %$proc.name
	%ifndef %$proc.argofs
		%error "`endproc' not in a proc context"
	%elifnidn %$proc.name,%1
		%define %$. %1
		%error "endproc names mismatch: expected `%$proc.name'"
		%error "but got `%$.' instead"
	%elif %$proc.locofs < %$proc.locsize
		%error	"unused local space declared (used %$proc.locofs, requested %$proc.locsize)"
	%else
%$exit:
	; Now pop the registers that we should restore on exit
		pop	%$proc.save
		%if %$proc.locsize != 0
		add	esp,%$proc.locsize
		%endif
		ret
__end_%1:
		%pop
	%endif
%endmacro
%pop

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   A replacement for "push" for use within procedures
; Arguments:
;   any number of registers which will be push'ed successively
; Example:
;		push	eax,ebx,ecx,edx
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		push	0-*
; dummy comment to avoid problems with "push" on the same line with a label
	%rep	%0
		push	%1
		%rotate	1
		%assign .$proc.stkofs .$proc.stkofs+4
	%endrep
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   A replacement for "pop" for use within procedures
; Arguments:
;   any number of registers which will be pop'ed in reverse order
; Example:
;		pop	eax,ebx,ecx,edx
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		pop	0-*
; dummy comment to avoid problems with "pop" on the same line with a label
	%rep	%0
		%rotate	-1
		pop	%1
		%assign .$proc.stkofs .$proc.stkofs-4
	%endrep
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Replacements for "pushfd" and "popfd" that takes care of esp
; Example:
;		pushfd
;		popfd
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		pushfd	0
		pushfd
		%assign .$proc.stkofs .$proc.stkofs+4
%endmacro
%macro		popfd	0
		popfd
		%assign .$proc.stkofs .$proc.stkofs-4
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Exit from current procedure (optionally on given condition)
; Arguments:
;   Either none or a condition code
; Example:
;		exit
;		exit	nz
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		exit	0-1 mp
		j%1	near %$exit
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   start an conditional branch
; Arguments:
;   A condition code
;   second (optional) argument - "short" (by default - "near")
; Example:
;		if	nz
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		if	1-2 near
; dummy comment to avoid problems with "if" on the same line with a label
		%push	if
		j%-1	%2 %$elseif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   define the "else" branch of a conditional statement
; Arguments:
;   optionaly: "short" if jmp to endif is less than 128 bytes away
; Example:
;		else
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		else	0-1
	%ifnctx if
		%error	"`else' without matching `if'"
	%else
		jmp	%1 %$endif
%$elseif:
		%define	%$elseif_defined
	%endif
%endmacro

;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
; Summary:
;   Finish am conditional statement
; Arguments:
;   none
; Example:
;		endif
;-----======xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx======-----
%macro		endif	0
	%ifnctx if
		%error	"`endif' without matching `if'"
	%else
		%ifndef %$elseif_defined
%$elseif:
		%endif
%$endif:
		%pop
	%endif
%endmacro

%endif ; __PROC32_ASH__
